execute_process(COMMAND bash -c "openssl x509 -noout -subject -in ../static_data/server.crt | sed 's/.*= *//'"
		OUTPUT_VARIABLE CERT_SUBJECT
		OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D'CERT_SUBJECT=\"${CERT_SUBJECT}\"'")

idf_component_register(SRCS "peripheral_test.c"
                    INCLUDE_DIRS "." "include"
                    EMBED_TXTFILES "../static_data/private.key" "../static_data/server.crt")
