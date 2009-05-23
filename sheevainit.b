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

	sys->bind("#c", "/dev", sys->MREPL);		# console
	sys->bind("#t", "/dev", sys->MAFTER);		# serial port
	sys->bind("#r", "/dev", sys->MAFTER);		# rtc
	sys->bind("#C", "/dev", Sys->MAFTER);		# crypto
	sys->bind("#o", "/dev", Sys->MAFTER);		# sdio
	sys->bind("#T", "/dev", Sys->MAFTER);		# sheeva
	sys->bind("#p", "/prog", sys->MREPL);
	sys->bind("#e", "/env", sys->MREPL|sys->MCREATE);
	sys->bind("#d", "/fd", Sys->MREPL);
	sys->bind("#I", "/net", Sys->MREPL);
	sys->bind("#l", "/net", Sys->MAFTER);

	rtc();

	sh := load Sh "/dis/sh.dis";
	sys->print("shell...\n");
	sh->init(nil, nil);
}

rtc()
{
	fd := sys->open("/dev/rtc", Sys->OREAD);
	if(fd == nil)
		return warn(sprint("rtc: %r"));
	n := sys->read(fd, buf := array[20] of byte, len buf);
	if(n <= 0)
		return warn(sprint("rtc: %r"));
	tmfd := sys->open("/dev/time", Sys->OWRITE);
	if(tmfd == nil)
		return warn(sprint("time: %r"));
	if(sys->fprint(tmfd, "%bd", big 1000000*big string buf[:n]) < 0)
		return warn(sprint("writing /dev/time: %r"));
}

warn(s: string)
{
	sys->fprint(sys->fildes(2), "%s\n", s);
}
