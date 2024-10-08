using System;
using System.Collections.Generic;
using System.Threading;
using System.Text;
using com.latencybusters.lbm;

namespace com.latencybusters.auxapi
{
    public static class Sequence
    {
        public static uint Difference(uint first, uint last)
        {
					uint diff;

					if ((int)(last - first) < 0)
							diff = (uint.MaxValue - last) + first;   //Wrap difference
					else
							diff = last - first;

					return diff;
        }

        public static uint Normalize(uint first, uint seq)
        {
            return seq - first;
        }
    }

    /******Extend Windows Semaphore*****/
    public class UMESemaphore
    {
        private Semaphore _sem;
        private int _max;
        private int _count;

        public UMESemaphore(int initCount, int maxCount)
        {
            _max = maxCount;
            _count = initCount;
            _sem = new Semaphore(initCount, maxCount);
        }

        public void Release()
        {
            _sem.Release();
            _count--;
        }

        public bool WaitOne(int mstimeout)
        {
            bool ret = false;

            ret = _sem.WaitOne(mstimeout, false);
            _count++;

            return ret;
        }

        public int GetCount()
        {
            return _count;
        }
    }

    public class UMEBitmap : List<bool>
    {
        public UMEBitmap(uint size) : base((int) size)
        {
            Inititalize(size);
        }

        public void Inititalize(uint size)
        {
            //clear if we got some entries
            if (Count > 0)
                Clear();

            for (int i = 0; i < size; i++)
                Add(false);
        }

        public void Set(uint idx, bool b)
        {
            this[(int) idx] = b;
        }

        public bool Validate()
        {
            for (int i = 0; i < Count; i++)
            {
                if (this[i] == false)
                    return false;
            }

            return true;
        }
    }

    public class UMEBlockSrc
    {
        private const int TIME_OUT = 30000;
        private const int RETRY_COUNT = 10;
        private int _err;
        internal UMEBitmap _map;
        internal LBMSource _src;
        internal LBMContext _ctx;
        internal LBMTopic _topic;
        internal UMESemaphore _stablelock;
        internal LBMSourceEventCallback _appcb;
        internal UMEBlockCB _umecb;
        internal object _clientd;
        internal int _maxretentionsz = -1;
        internal uint _last;
        internal uint _first;

        public UMEBlockSrc()
        { }

        public bool createSource(LBMContext lbmctx, LBMTopic lbmtopic, LBMSourceAttributes sattr, LBMSourceEventCallback cb, object cbArg, LBMEventQueue lbmevq)
        {
            _ctx = lbmctx;
            _topic = lbmtopic;
            _clientd = cbArg;
            _appcb = cb;
            _umecb = new UMEBlockCB(this);
            _map = new UMEBitmap(10);
            _err = 0;

            _stablelock = new UMESemaphore(0, 1);

            _src = _ctx.createSource(_topic, new LBMSourceEventCallback(_umecb.onSourceEvent), _clientd, lbmevq);

            _maxretentionsz = Convert.ToInt32(_src.getAttributeValue("retransmit_retention_size_limit"));

            return ((_src == null)? false : true);
        }

        public void send(byte[] message, int messagelength, int flags, LBMSourceSendExInfo exinfo)
        {
            LBMSourceSendExInfo tinfo = new LBMSourceSendExInfo();
            int retry = 0;
            bool run = true;

            if (messagelength > _maxretentionsz)
            {
                throw new LBMMonitorENoMemException("message bigger than retention buffer");
            }

            _err = 0;
            //Set ex flag
            tinfo.setFlags(LBM.SRC_SEND_EX_FLAG_SEQUENCE_NUMBER_INFO);
            if (exinfo != null)
            {
                tinfo.setFlags(exinfo.flags() | tinfo.flags());
            }

            //Clear out the LBM_SRC_NONBLOCK flag
            flags &= ~(LBM.SRC_NONBLOCK);
            flags |= LBM.SRC_BLOCK | LBM.MSG_FLUSH;

            
            //Send the message
            while (run)
            {
                try
                {
                    _src.send(message, messagelength, flags, tinfo);
                    run = false;
                }
                catch(UMENoRegException ex)
                {
                    Thread.Sleep(500);
                    retry++;

                    if (retry == RETRY_COUNT)
                    {
                        throw ex;
                    }

                }
                catch(Exception ex)
                {
                    Console.WriteLine("Tried registering to a store " + Convert.ToString(RETRY_COUNT) + " times. Giving up block");
                    throw ex;
                }
            }

            //Gain a lock on stable
            while (!_stablelock.WaitOne(TIME_OUT))
            {
                Console.Error.WriteLine("Warning: Waited " + (TIME_OUT / 1000).ToString() + " seconds. Timeout hit.");
            }

            if (_err != 0)
            {
                throw new LBMException(_err, "unable to register to store");
            }
        }

        public void close()
        {
            _src.close();
        }

        /* Properties */
        public LBMContext Context
        {
            get { return _ctx; }
        }

        public UMESemaphore StableLock
        {
            get { return _stablelock; }
        }

        public LBMSourceEventCallback AppCb
        {
            get { return _appcb; }
        }

        public object Clientd
        {
            get { return _clientd; }
        }

        public int Error
        {
            get { return _err; }
            set { _err = value; }
        }

        public UMEBitmap Map
        {
            get { return _map; }
            set { _map = value; }
        }

        public uint First
        {
            get { return _first; }
            set { _first = value; }
        }

        public uint Last
        {
            get { return _last; }
            set { _last = value; }
        }
        /************************************************/

    }

    /******* Block Callback Class *******/
    class UMEBlockCB
    {
        private UMEBlockSrc _blk;

        public UMEBlockCB(UMEBlockSrc blk)
        {
            _blk = blk;
        }

        public void onSourceEvent(Object arg, LBMSourceEvent sourceEvent)
        {
            uint idx;

            switch (sourceEvent.type())
            {
                case LBM.SRC_EVENT_UME_MESSAGE_STABLE:
                    UMESourceEventAckInfo stInfo = sourceEvent.ackInfo();

                    idx = Sequence.Normalize(_blk.First, stInfo.sequenceNumber());
                    _blk.Map.Set(idx, true);

                    if (_blk.Map.Validate())
                        _blk.StableLock.Release();

                    break;

                case LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX:
                    UMESourceEventAckInfo stInfoex = sourceEvent.ackInfo();

                    if ((stInfoex.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE) != 0)
                    {
                        /* Set bit in bitmap */
                        idx = Sequence.Normalize(_blk.First, stInfoex.sequenceNumber());
                        _blk.Map.Set(idx, true);

                        if (_blk.Map.Validate())
                            _blk.StableLock.Release();
                    }

                    break;

                case LBM.SRC_EVENT_SEQUENCE_NUMBER_INFO:
                    LBMSourceEventSequenceNumberInfo info = sourceEvent.sequenceNumberInfo();
                    uint seq;
                    _blk.Last  = info.lastSequenceNumber();
                    _blk.First = info.firstSequenceNumber();
                    seq = Sequence.Difference(_blk.First, _blk.Last) + 1;

                    /* Initialize bitmap */
                    _blk.Map.Inititalize(seq);

                    break;

                case LBM.SRC_EVENT_UME_STORE_UNRESPONSIVE:

                    if (_blk.StableLock.GetCount() == 0)
                    {
                        Console.WriteLine("Warning: UME store unresponsive while waiting for stability event");
                    }

                    break;
            }

            if (_blk.AppCb != null)
                _blk.AppCb(_blk.Clientd, sourceEvent);
        }
    }
    /************************************/
}
