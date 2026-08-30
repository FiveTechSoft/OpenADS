# Merges several static archives into one using GNU ar's MRI script
# mode. add_custom_command runs without a shell, so `ar -M < script`
# redirection has to go through execute_process(INPUT_FILE) instead.
#
# Usage:
#   cmake -DAR=<gnu-ar> -DMRI=<script.mri> \
#         -DOUT_TMP=<merged.tmp.a> -DOUT_FINAL=<final.a> \
#         -P cmake/merge_ar.cmake
#
# The MRI script must `create ${OUT_TMP}`, `addlib` every input
# archive, then `save` / `end`. Paths in it must be space-free.

execute_process(
    COMMAND "${AR}" -M
    INPUT_FILE "${MRI}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "ar -M failed (rc=${_rc}) while merging ${OUT_FINAL}\n${_out}${_err}")
endif()

# file(RENAME) refuses to overwrite an existing destination.
file(REMOVE "${OUT_FINAL}")
file(RENAME "${OUT_TMP}" "${OUT_FINAL}")
