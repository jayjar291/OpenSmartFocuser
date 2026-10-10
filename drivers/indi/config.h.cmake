#ifndef CONFIG_H
#define CONFIG_H

/* CMake substitutes the discovered INDI data directory into generated config.h. */
#cmakedefine INDI_DATA_DIR "@INDI_DATA_DIR@"

/* Keep the runtime DRIVER_INFO version in sync with CMake project metadata. */
#define CDRIVER_VERSION_MAJOR @CDRIVER_VERSION_MAJOR@
#define CDRIVER_VERSION_MINOR @CDRIVER_VERSION_MINOR@

#endif /* CONFIG_H */
