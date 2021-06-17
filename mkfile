<$PLAN9/src/mkhdr

MKSHELL=rc

CFLAGS=$CFLAGS

UPDATEFLAGS=

HFILES=\
	stdinc.h\
	dat.h\
	fns.h\
	aws.h\

TARG=\
	s3\
	s3mkarena\
	s3venti\

FILES=\
	aws\
	config\
	crypt\
	ifile\
	lumpcache\
	part\
	score\
	s3block\
	utils\
	unittoull\
	unwhack\
	whack\
	zblock\
#	clump\
#	conv\
#	lump\
#	lumpqueue\
#	stats\

SLIB=libs3.a
CLEANFILES=$SLIB core.*

LIB=\
	$SLIB\

LIBCFILES=${FILES:%=%.c}
LIBOFILES=${FILES:%=%.$O}

CFILES=${TARG:%=%.c} $LIBCFILES

UPDATE=mkfile\
	$HFILES\
	$CFILES\

BIN=$home/bin

it:V: all

<$PLAN9/src/mkmany

%.acid: %.$O $HFILES
	$CC $CFLAGS -a $stem.c >$target

$SLIB(%.$O):N: %.$O
$SLIB:	${LIBOFILES:%=$SLIB(%)}
	names = `{echo $newprereq |sed 's/ /\n/g' |9 sed -n 's/'$SLIB'\(([^)]+)\)/\1/gp'}
	ar vcr $SLIB $names

s3test:VQ: $O.s3
	flag x +
	$O.s3 create rcbilson-test
	$O.s3 write rcbilson-test fortunes $PLAN9/lib/fortunes
	$O.s3 read rcbilson-test fortunes >test.xxx
	cmp $PLAN9/lib/fortunes test.xxx
	rm test.xxx
	$O.s3 write rcbilson-test units $PLAN9/lib/units
	$O.s3 ls
	$O.s3 ls rcbilson-test
	$O.s3 ls rcbilson-test fort
	$O.s3 rm rcbilson-test fortunes units
	$O.s3 rm rcbilson-test
	$O.s3 ls
