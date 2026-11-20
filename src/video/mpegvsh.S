/* psp5 authentic – import of sceMpegGetAvcNalAu of the library sceMpeg (NID 0x11F95CF1, the same in PPSSPP's list of the library). The PSPSDK's stubs do not have it: it takes a sample of an MP4
 * (the NAL units with their lengths in front, as the file has them) and the parameter sets, and makes the access unit that sceMpegAvcDecode wants; see hwavc.h. */
	.set noreorder

#include "pspstub.s"

	STUB_START "sceMpeg",0x40090000,0x00010005
	STUB_FUNC  0x11F95CF1,sceMpegGetAvcNalAu
	STUB_END
