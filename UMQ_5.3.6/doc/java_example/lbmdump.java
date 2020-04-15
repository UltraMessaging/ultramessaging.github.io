package com.latencybusters.lbm;

/*
  All of the documentation and software included in this and any
  other Informatica Corporation Ultra Messaging Releases
  Copyright (C) Informatica Corporation. All rights reserved.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted only as covered by the terms of a
  valid software license agreement with Informatica Corporation.

  Copyright (C) 2004-2014, Informatica Corporation. All Rights Reserved.

  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES 
  EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF 
  NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR 
  PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE 
  UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES, BE 
  LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR 
  INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE 
  TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF 
  THE LIKELIHOOD OF SUCH DAMAGES.

  The content of LBMSDM.java should be aligned with the C header lbmsdm.h
*/


/**
This class is a general utility to aid dumping blocks of data in
a debuggable (hex) form. It provides static methods to dump
to the console.
*/

public class lbmdump
{
	/**
	Boolean to control whether the offset is displayed at the
	start of every line dumped. Defaults to true.
	*/
	public static boolean dump_offset = true;

	/**
	Get a string of hex formatted characters
	@param buf The buffer containing the data to be converted to hex
	@param offset The offset in to the buffer to start processing
	@param len The number of bytes to be formatted
	@return A string of hex formatted characters
	*/
	public static String toHexString(byte [] buf, int offset, int len) {

		String s = "";
		for(int i = 0; i < len; i++) {
			int b = (int) buf[offset + i]; if(b < 0) b += 256;
			if(b < 16) s += "0";
			s += Integer.toHexString(b);
		}
		return s;
	}

	/**
	Get a string of hex formatted characters - the entire buffer is Xi
	processed
	@param buf The buffer containing the data to be converted to hex
	@return A string of hex formatted characters
	*/
	public static String toHexString(byte [] buf) {
		return toHexString(buf,0,buf.length);
	}

	/**
	Dump a buffer to stdout
	@param buf The buffer containing the data to be converted to hex
	*/
	public static void dump(byte[] buf) {
		dump(buf,0,buf.length);
	}

	/**
	Dump a buffer to stdout
	@param buf The buffer containing the data to be converted to hex
	@param start The offset in to the buffer to start processing
	@param end The penultimate byte to be formatted
	*/
	public static void dump(byte[] buf,int start,int end) {
		int i,width = 16;
		String s = "";
		String as = "";
		if(dump_offset) {
			Integer base = new Integer(((start + 1) / width) * width);
			if(base < 16) s+=" ";
			if(base < 256) s+=" ";
			if(base < 4096) s+=" ";
		        s += base.toHexString(base) + ": ";
		}
		s += " ";
		for(i = 0; i < start; i++) {
			s += "  ";
			as += " ";
			if(i % 2 == 1) s += " ";
		}
		for(i = start; i < end; i++) {
			int b = (int) buf[i]; if(b < 0) b += 256;
			if(b < width) s += "0";

			s += Integer.toHexString(b);
			char c = (char) (buf[i] & 0x7f);
			if(Character.isISOControl(c) || (buf[i] & 0x80) != 0)
				as = as + ".";
			else
				as = as + c;

			if(i % 2 == 1) s += " ";
			if(i % width == width - 1) {
				System.out.println(s + " " + as);
				s = "";
				if(dump_offset) {
					Integer base = new Integer(((i + 1) / width) * width);
					if(base < 16) s+=" ";
					if(base < 256) s+=" ";
					if(base < 4096) s+=" ";
						s += base.toHexString(base) + ": ";
				}
				s += " ";
				as = "";
			}
		}
		if(!s.equals(" ")) {
			for(i = i % width; i < width; i++) {
				s += "  ";
				as += " ";
				if(i % 2 == 1) s += " ";
			}
			System.out.println(s + " " + as);
		}
	}

}

