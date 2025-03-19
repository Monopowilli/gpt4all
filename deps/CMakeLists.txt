set(BUILD_SHARED_LIBS OFF)

set(FMT_INSTALL OFF)
add_subdirectory(fmt)

set(BUILD_TESTING           OFF)
set(QCORO_BUILD_EXAMPLES    OFF)
set(QCORO_WITH_QTDBUS       OFF)
set(QCORO_WITH_QTWEBSOCKETS OFF)
set(QCORO_WITH_QTQUICK      OFF)
set(QCORO_WITH_QTTEST       OFF)
add_subdirectory(qcoro)

set(GPT4ALL_BOOST_TAG 1.87.0)
FetchContent_Declare(
    boost
    URL "https://github.com/boostorg/boost/releases/download/boost-${GPT4ALL_BOOST_TAG}/boost-${GPT4ALL_BOOST_TAG}-cmake.tar.xz"
    URL_HASH "SHA256=7da75f171837577a52bbf217e17f8ea576c7c246e4594d617bfde7fafd408be5"
)

set(BOOST_INCLUDE_LIBRARIES json describe system)
FetchContent_MakeAvailable(boost)
