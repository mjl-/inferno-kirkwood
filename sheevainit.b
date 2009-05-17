implement Init;

include "sys.m";
	sys: Sys;
	sprint: import sys;
include "draw.m";
include "sh.m";

Init: module
{
	init:	fn();
};

init()
{
	sys = load Sys Sys->PATH;
	
	sys->print("sheevainit...\n");

	sys->bind("#c", "/dev", sys->MAFTER);
	sys->bind("#r", "/dev", sys->MAFTER);
	sys->bind("#c", "/dev", sys->MREPL);			# console
	sys->bind("#t", "/dev", sys->MAFTER);		# serial port
	sys->bind("#r", "/dev", sys->MAFTER);		# RTC
	sys->bind("#p", "/prog", sys->MREPL);		# prog device
	sys->bind("#d", "/fd", Sys->MREPL);

	rtc();

	sh := load Sh "/dis/sh.dis";
	sys->print("shell...\n");
	sh->init(nil, nil);
}

rtc()
{
	fd := sys->open("/dev/rtc", Sys->OREAD);
	if(fd == nil)
		return warn("rtc: %r");
	n := sys->read(fd, buf := array[20] of byte, len buf);
	if(n <= 0)
		return warn("rtc: %r");
	tmfd := sys->open("/dev/time", Sys->OWRITE);
	if(tmfd == nil)
		return warn("time: %r");
	if(sys->fprint(tmfd, "%bd", big 1000000*big string buf[:n]) < 0)
		return warn("writing /dev/time: %r");
}

warn(s: string)
{
	sys->fprint(sys->fildes(2), "%s\n", s);
}
