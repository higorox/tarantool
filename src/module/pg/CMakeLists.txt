# A special hack for buggy FindPostgreSQL.cmake in Debian/Ubuntu
set(PostgreSQL_ROOT /usr/include/postgresql;/usr/include/postgres)
find_optional_package(PostgreSQL)

if (PostgreSQL_FOUND)
	include_directories(${PostgreSQL_INCLUDE_DIRS})
	add_library(pg SHARED pg.cc)
	target_link_libraries(pg ${PostgreSQL_LIBRARIES} -rdynamic)
	set_target_properties(pg PROPERTIES PREFIX "")
	install(TARGETS pg LIBRARY DESTINATION ${MODULE_DIR}/box/net)
else()
	message(STATUS "PostgreSQL client not found, box.net.sql(pg) disabled")
endif()
