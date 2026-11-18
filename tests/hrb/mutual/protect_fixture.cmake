# POSIX mock fixtures must meet the same owner-only mode as real modules.
if(UNIX)
    file(CHMOD "${HRB}" PERMISSIONS OWNER_READ OWNER_WRITE)
endif()
# Windows extraction ACL requirements are documented in ACL-SETUP.txt.
