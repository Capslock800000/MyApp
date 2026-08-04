cmake_minimum_required(VERSION 3.22.1)
project("shell")

file(GLOB SRCS src/main/cpp/*.cpp src/main/cpp/*.c)

add_library(security SHARED ${SRCS})

find_library(log-lib log)

if(ANDROID)
    # ============================================================
    # Android 交叉编译：使用预编译的静态库
    # ============================================================
    
    # --- OpenSSL ---
    if(NOT OPENSSL_BASE_DIR)
        message(FATAL_ERROR "OPENSSL_BASE_DIR is not set. Pass -DOPENSSL_BASE_DIR=<path> to CMake.")
    endif()
    
    set(OPENSSL_ROOT_DIR "${OPENSSL_BASE_DIR}/${ANDROID_ABI}")
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.a")
    
    if(NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
        message(FATAL_ERROR "OpenSSL crypto library not found at ${OPENSSL_CRYPTO_LIBRARY}")
    endif()
    
    add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
    )
    
    # --- libzip ---
    if(NOT LIBZIP_BASE_DIR)
        message(FATAL_ERROR "LIBZIP_BASE_DIR is not set. Pass -DLIBZIP_BASE_DIR=<path> to CMake.")
    endif()
    
    set(LIBZIP_ROOT_DIR "${LIBZIP_BASE_DIR}/${ANDROID_ABI}")
    set(LIBZIP_INCLUDE_DIRS "${LIBZIP_ROOT_DIR}/include")
    set(LIBZIP_LIBRARY "${LIBZIP_ROOT_DIR}/lib/libzip.a")
    
    if(NOT EXISTS "${LIBZIP_LIBRARY}")
        message(FATAL_ERROR "libzip library not found at ${LIBZIP_LIBRARY}")
    endif()
    
    add_library(libzip::libzip STATIC IMPORTED GLOBAL)
    set_target_properties(libzip::libzip PROPERTIES
        IMPORTED_LOCATION "${LIBZIP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBZIP_INCLUDE_DIRS}"
    )
    
    find_library(z-lib z)
    set(LIBZIP_LINK libzip::libzip)
    set(EXTRA_LIBS ${z-lib})
else()
    # ============================================================
    # 宿主机构建（本地开发）：保持原有逻辑
    # ============================================================
    find_package(OpenSSL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBZIP REQUIRED libzip)
    
    set(LIBZIP_LINK ${LIBZIP_LIBRARIES})
    set(EXTRA_LIBS "")
endif()

target_link_libraries(security 
    ${log-lib} 
    ${EXTRA_LIBS} 
    ${LIBZIP_LINK} 
    OpenSSL::Crypto
)
target_include_directories(security PRIVATE ${LIBZIP_INCLUDE_DIRS})
