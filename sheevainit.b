#
# sheeva
#

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


#
# initialise flash translation
# mount flash file system
# add devices
# start a shell or window manager
#

init()
{
	sys = load Sys Sys->PATH;
	
	sys->print("sheevainit...\n");

	dobind("#I", "/net", Sys->MREPL);		# IP
	dobind("#l", "/net", Sys->MAFTER);		# ether
	dobind("#c", "/dev", sys->MREPL);		# console
	dobind("#t", "/dev", sys->MAFTER);		# serial line
	dobind("#r", "/dev", sys->MAFTER);		# rtc
	dobind("#p", "/prog", sys->MREPL);
	dobind("#e", "/env", sys->MREPL|sys->MCREATE);
	dobind("#d", "/fd", Sys->MREPL);

	setclock("#r/rtc");

	setsysname("sheeva");

	start("sh", "-c" :: "run /init" :: nil);
}

setsysname(def: string)
{
	v := array of byte def;
	fd := sys->open("/env/sysname", sys->OREAD);
	if(fd != nil){
		buf := array[Sys->NAMEMAX] of byte;
		nr := sys->read(fd, buf, len buf);
		if(nr > 0)
			v = buf[0:nr];
	}
	fd = sys->open("/dev/sysname", sys->OWRITE);
	if(fd != nil)
		sys->write(fd, v, len v);
}

setclock(file: string)
{
	fd := sys->open(file, Sys->OREAD);
	if(fd == nil)
		return warn(sprint("%s: %r", file));
	n := sys->read(fd, buf := array[20] of byte, len buf);
	if(n <= 0)
		return warn(sprint("%s: %r", file));
	tmfd := sys->open("/dev/time", Sys->OWRITE);
	if(tmfd == nil)
		return warn(sprint("time: %r"));
	if(sys->fprint(tmfd, "%bd", big 1000000*big string buf[:n]) < 0)
		return warn(sprint("writing /dev/time: %r"));
}

start(cmd: string, args: list of string)
{
	disfile := cmd;
	if(disfile[0] != '/')
		disfile = "/dis/"+disfile+".dis";
	(ok, nil) := sys->stat(disfile);
	if(ok >= 0){
		dis := load Command disfile;
		if(dis == nil)
			sys->print("init: can't load %s: %r\n", disfile);
		else
			spawn dis->init(nil, cmd :: args);
	}
}

dobind(f, t: string, flags: int): int
{
	if((ret := sys->bind(f, t, flags)) < 0)
		warn(sys->sprint("can't bind %s on %s: %r", f, t));
	return ret;
}

warn(s: string)
{
	sys->fprint(sys->fildes(2), "%s\n", s);
}
