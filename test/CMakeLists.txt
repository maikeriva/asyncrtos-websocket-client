idf_component_register(
    SRC_DIRS
        "."
    INCLUDE_DIRS
        "priv_include"
    REQUIRES
        "unity"
        "esp-tls"
        "asyncrtos"
        "asyncrtos-wifi"
        "asyncrtos-websocket-client"
    EMBED_TXTFILES
        "postman_echo_com.pem"
)
