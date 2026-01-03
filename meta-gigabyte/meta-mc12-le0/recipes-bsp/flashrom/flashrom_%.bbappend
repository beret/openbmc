# Example .bbappend file to fix the buildpaths QA issue

#do_install_append() {
#    # Replace the absolute path in the problematic file(s) with the correct variable or relative path
#    # Replace "path/to/problematic_file" with the actual file path from your log
#    # Replace "/home/youruser/yocto/build/tmp..." with the appropriate variable, 
#    # often ${STAGING_DIR_TARGET} or similar context variable.
#    sed -i 's|/home/youruser/yocto/build/tmp|${STAGING_DIR_TARGET}|g' ${D}/path/to/problematic_file
#}

# If the issue is in the compiler flags used during build, 
# you might need to fix how it uses CFLAGS or LDFLAGS in the original recipe 
# (often by using the standard autotools or cmake bbclasses correctly).


# Looks similar to https://github.com/openbmc/openbmc/issues/4053
# Does not affect romulus currently, but always for mx12-le0 on 1.5.1, no apparent changes related
# to this in newer versions of flashrom either (untested)
# To ignore the specific QA issue for this package if a fix isn't feasible immediately:
# INSANE_SKIP:${PN}-staticdev += "buildpaths"
INSANE_SKIP:flashrom-staticdev += "buildpaths"
