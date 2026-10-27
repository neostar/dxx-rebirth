# shellcheck shell=bash
# The frame hash lines rtrace logs for the committed synthetic traces, as
# Apple's GL draws them (M1 Max, macOS 26.7); sourced by rtrace-tests.sh
# (R1-style GL pins and R3 on Metal) and rtrace-d3d12.sh (R3 on D3D12).
# The sourcing script sets root, the repository's top directory.
# shellcheck disable=SC2034,SC2154 # set and read by the sourcing scripts

# The sample: frame 1 is 64x48 pixels of (255, 255, 0), hashed with 64-bit
# FNV-1a by python3 when written; frame 0, the shaded triangle.
expected=('0 64x48 86388f1d5003af2a' '1 64x48 9f40fbb36ef64325')
sample=$root/common/render/rtrace/synthetic/sample.rtr
# The overbright trace (#27).
expected_overbright=('0 64x48 5d1b6b445fb7f05a' '1 64x48 418a435cc9c67e56')
overbright=$root/common/render/rtrace/synthetic/overbright.rtr
# The flash trace (#30).
expected_flash=('0 64x48 a088becd21e46f87' '1 64x48 4142e7869f743d59' '2 64x48 c33733e407fec502')
flash=$root/common/render/rtrace/synthetic/flash.rtr
