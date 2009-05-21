<../../mkconfig

CONF=sheeva
CONFLIST=sheeva

SYSTARG=$OSTARG
OBJTYPE=arm
INSTALLDIR=$ROOT/Inferno/$OBJTYPE/bin

<$ROOT/mkfiles/mkfile-$SYSTARG-$OBJTYPE

<| $SHELLNAME ../port/mkdevlist $CONF

KTZERO=0x8000
KLOAD=0x7fe0  # $KTZERO-0x20 (size of a.out header)

OBJ=\
	l.$O\
	clock.$O\
	fpi.$O\
	fpiarm.$O\
	fpimem.$O\
	trap.$O\
	main.$O\
	$CONF.root.$O\
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

install:V: $INSTALLDIR/i$CONF $INSTALLDIR/i$CONF $INSTALLDIR/ui$CONF $INSTALLDIR/ui$CONF.gz

inst:V: default
	vers=`date +%s`
	dst=/n/tftp/ui$CONF.gz-$vers
	install -m644 ui$CONF.gz $dst
	ln -sf ui$CONF.gz-$vers /n/tftp/ui$CONF.gz

i$CONF: $OBJ $CONF.c $CONF.root.h $LIBNAMES
	$CC $CFLAGS '-DKERNDATE='$KERNDATE $CONF.c
	$LD -o $target -T $KTZERO -R4 -l $OBJ $CONF.$O $LIBFILES


ui$CONF: i$CONF
	mkuimage $KLOAD $KTZERO i$CONF >$target

ui$CONF.gz: i$CONF
	gzip <i$CONF >i$CONF.gz
	mkuimage -C gzip $KLOAD $KTZERO i$CONF.gz >$target

<../port/portmkfile

../init/$INIT.dis: ../init/$INIT.b
	cd ../init; mk $INIT.dis

main.$O:	$ROOT/Inferno/$OBJTYPE/include/ureg.h

vclean:V: clean
	rm -f ui$CONF ui$CONF.gz
