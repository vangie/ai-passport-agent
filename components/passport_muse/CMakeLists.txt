set(noise upstream/noise)
set(setup upstream/setup)
idf_component_register(
    SRCS "${noise}/src/ClientSession.cpp" "${noise}/src/InitiatorHandshake.cpp"
         "${noise}/src/MbedtlsCryptoBackend.cpp" "${noise}/src/ServiceCodec.cpp"
         "${noise}/src/Status.cpp" "${noise}/src/Transport.cpp"
         "${noise}/src/TransportFrameCodec.cpp"
         "${setup}/src/mg_link_setup.c" "${setup}/src/mgls_util.c"
         "${setup}/src/mg_token_proof.c"
         "${setup}/src/crypto/mgls_crypto_builtin.c" "${setup}/src/crypto/mgls_uecc.c"
    INCLUDE_DIRS "${noise}/include" "${setup}/include"
    PRIV_INCLUDE_DIRS "${setup}/src" "${setup}/src/crypto"
    PRIV_REQUIRES mbedtls
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MGLS_ENABLE_LOG=1)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
