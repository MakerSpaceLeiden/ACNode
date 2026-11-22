cd examples 
export TAG=$(git describe)

D=$(date  +$TAG-%Y%m%d%H%M%S)

OUTDIR=${OUTDIR:-/tmp/foobar/out-$D}
BUILDDIR=${BUILDDIR:-/tmp/foobar/build}

board=esp32:esp32:esp32da
mkdir -p "$OUTDIR" "$BUILDDIR"

for node in SurfaceGrinderNode TableSawNode WoodLatheNode PlanerNode JointerNode OlgaByeNode AbeneNode BandsawNode CeramicsNode DustbinNode MetalMitreSawNode OlgaBinnenNode OlgaBuitenNode OlgaByeNode OlgaStoreNode PedestalGrinderNode
do
	(
		set -e
		cd $node

		# Make the builds somewhat reproducable; by using the 
		# commit date of the last commit.
 		#
  		export DATE=$(TZ=UTC0 git log -1 --date=local --format=%cd)
		export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)

		# Extract flags so we can add ours to it. We could aso do this with 
		# the extra_defines flags perhaps.
		#
		all_flags=$(arduino-cli compile -b ${board} . --show-properties |grep ^build.extra_flags=)

		echo bulding $node esp32:esp32:esp32da:PartitionScheme=min_spiffs as $TAG on $DATE GMT
		/opt/local/bin/arduino-cli compile \
			--output-dir "$OUTDIR" \
			--build-path "$BUILDDIR" \
			--fqbn  "$board:UploadSpeed=921600,CPUFreq=240,FlashFreq=80,FlashMode=qio,FlashSize=4M,PartitionScheme=min_spiffs,DebugLevel=none,LoopCore=1,EventsCore=1,EraseFlash=none" \
			--jobs 120 \
			--build-property build.partitions=min_spiffs \
			--build-property "$all_flags -DTAG=\"$TAG\"" \
			--verbose \
			. # >"$BUILDDIR/compile-$node.log" 2&>1 
		cd "$OUTDIR"
		rm -f *.bootloader.bin *.map *.merged.bin *.partitions.bin
	) || exit $?
done
# rm -rf  "$BUILDDIR" 
exit 0
#			--build-property "$all_flags -DTAG=\"$TAG\"" \
