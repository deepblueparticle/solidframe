if(${Boost_FOUND} AND OPENSSL_FOUND)
    add_executable (example_echo_server example_echo_server.cpp)

    add_dependencies(example_echo_server build_boost)

    target_link_libraries (example_echo_server
        solid_frame
        solid_frame_aio
        solid_serialization_v1
        solid_utility
        solid_system
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        ${SYS_BASIC_LIBS}
        ${SYS_DYNAMIC_LOAD_LIB}
    )

    #if(SOLID_ON_WINDOWS)
    #    set(SUFFIX "${CMAKE_BUILD_TYPE}")
    #endif()

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/echo-ca-cert.pem ${CMAKE_CURRENT_BINARY_DIR}/echo-server-key.pem ${CMAKE_CURRENT_BINARY_DIR}/echo-server-cert.pem ${CMAKE_CURRENT_BINARY_DIR}/echo-client-key.pem ${CMAKE_CURRENT_BINARY_DIR}/echo-client-cert.pem
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-ca-cert.pem ${CMAKE_CURRENT_BINARY_DIR}/${SUFFIX}/
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-server-key.pem ${CMAKE_CURRENT_BINARY_DIR}/${SUFFIX}/
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-server-cert.pem ${CMAKE_CURRENT_BINARY_DIR}/${SUFFIX}/
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-client-key.pem ${CMAKE_CURRENT_BINARY_DIR}/${SUFFIX}/
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-client-cert.pem ${CMAKE_CURRENT_BINARY_DIR}/${SUFFIX}/
        DEPENDS ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-ca-cert.pem ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-server-key.pem ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-server-cert.pem ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-client-key.pem ${PROJECT_SOURCE_DIR}/tutorials/certs/echo-client-cert.pem
    )

    add_custom_target(example_secure_certs
        DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/echo-ca-cert.pem
        ${CMAKE_CURRENT_BINARY_DIR}/echo-server-key.pem
        ${CMAKE_CURRENT_BINARY_DIR}/echo-server-cert.pem
        ${CMAKE_CURRENT_BINARY_DIR}/echo-client-key.pem
        ${CMAKE_CURRENT_BINARY_DIR}/echo-client-cert.pem
    )

    add_executable (example_secure_echo_server example_secure_echo_server.cpp)

    add_dependencies(example_secure_echo_server example_secure_certs build_boost build_openssl)

    target_link_libraries (example_secure_echo_server
        solid_frame
        solid_frame_aio
        solid_frame_aio_openssl
        solid_serialization_v1
        solid_utility
        solid_system
        ${OPENSSL_LIBRARIES}
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        ${SYS_BASIC_LIBS}
        ${SYS_DYNAMIC_LOAD_LIB}
    )

    add_executable (example_secure_echo_client example_secure_echo_client.cpp)

    add_dependencies(example_secure_echo_client example_secure_certs build_boost build_openssl)

    target_link_libraries (example_secure_echo_client
        solid_frame
        solid_frame_aio
        solid_frame_aio_openssl
        solid_serialization_v1
        solid_utility
        solid_system
        ${OPENSSL_LIBRARIES}
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        ${SYS_BASIC_LIBS}
        ${SYS_DYNAMIC_LOAD_LIB}
    )

    add_executable (example_echo_auto_client example_echo_auto_client.cpp)

    add_dependencies(example_echo_auto_client build_boost)

    target_link_libraries (example_echo_auto_client
        solid_frame
        solid_frame_aio
        solid_utility
        solid_system
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        ${SYS_BASIC_LIBS}
    )
endif()




