<../../mkconfig

CONF=sheeva
CONFLIST=sheeva

SYSTARG=$OSTARG
OBJTYPE=arm
INSTALLDIR=$ROOT/Inferno/$OBJTYPE/bin

<$ROOT/mkfiles/mkfile-$SYSTARG-$OBJTYPE

<| $SHELLNAME ../port/mkdevlist $CONF

OBJ=\
	l.$O\
	main.$O\
	$CONF.root.$O\
	clock.$O\
	trap.$O\
	archkirkwood.$O\
	$IP\
	$DEVS\
	$ETHERS\
	$LINKS\
	$PORT\
	$MISC\
	$OTHERS\

LIBNAMES=${LIBS:%=lib%.a}
LIBDIRS=$LIBS

HFILES=\
	mem.h\
	dat.h\
	fns.h\
	io.h\

CFLAGS=-wFV -I$ROOT/Inferno/$OBJTYPE/include -I$ROOT/include -I$ROOT/libinterp
KERNDATE=`{$NDATE}

default:V: i$CONF ui$CONF ui$CONF.gz

install:V: $INSTALLDIR/i$CONF $INSTALLDIR/i$CONF

inst:V: default
	vers=`date +%s`
	dst=/n/tftp/ui$CONF.gz-$vers
	install -m644 ui$CONF.gz $dst
	ln -sf ui$CONF.gz-$vers /n/tftp/ui$CONF.gz

i$CONF: $OBJ $CONF.c $CONF.root.h $LIBNAMES
	$CC $CFLAGS '-DKERNDATE='$KERNDATE $CONF.c
	$LD -o $target -T 0x8000 -R4 -l $OBJ $CONF.$O $LIBFILES

ui$CONF: i$CONF
	mkuimage 0x7fe0 0x8000 i$CONF >$target

ui$CONF.gz: i$CONF
	gzip <i$CONF >i$CONF.gz
	mkuimage -C gzip 0x7fe0 0x8000 i$CONF.gz >$target

<../port/portmkfile

../init/$INIT.dis: ../init/$INIT.b
	cd ../init; mk $INIT.dis

main.$O:	$ROOT/Inferno/$OBJTYPE/include/ureg.h
