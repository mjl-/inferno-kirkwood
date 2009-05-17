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

	sh := load Sh "/dis/sh.dis";
	sys->print("shell...\n");
	sh->init(nil, nil);
}
