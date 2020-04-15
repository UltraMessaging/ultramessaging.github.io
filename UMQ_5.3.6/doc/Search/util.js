
/*
 *	Notes on Chrome
 *
 *	Chrome has a security feature where javascript cannot be used to access
 *	frames in a different domain, protocol, or port.  Similarly, Chrome has a
 *	bug where it sees frames in the same local directory as being in different
 *	domains.  For this reason, removeFrame() does not work on Chrome, because
 *	I get the URL of the other frame, and make it the location of the main window.
 *	For now, the close frame button is disabled in Chrome.
 *
 */
 
var isChrome = navigator.userAgent.toLowerCase().indexOf('chrome') > -1;
var isIE = navigator.userAgent.toLowerCase().indexOf('msie') > -1;

function addCloseFrame() {

	if (!isChrome) {
		document.writeln("<div id=\"close_frame\">");
		document.writeln("<a href=\"#\" onclick=\"javascript:removeFrame()\" class=\"text\">close <b>x</b></a>");
		document.writeln("</div>");
	}	
}
 
function removeFrame() {

	if (isIE) {
    	var frameset = window.top.index;
    	frameset.cols = "0, *";
    } else {	
		var url = parent.doccontent.location;
		top.location.href = url;
	}
}