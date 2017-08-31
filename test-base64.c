#include "base64.h"
#include "buffer-static.h"
#include "common-static.h"
#include "option.h"

int
main(int argc, char **argv)
{
    size_t size = 8 * 1024 * 1024;

    option(opt_int, &size, "size", "");
    option_run(argc, argv);

    int randomfd = safe_open("/dev/urandom", O_RDONLY);

    if (randomfd < 0)
        return -1;

    buffer_t rnd;
    buffer_setup(&rnd, safe_malloc(size), size);

    info("generating %zu random bytes...\n", size);

    if (safe_read(randomfd, rnd.data, size) != size)
        error("read");

    rnd.write += size;

    safe_close(randomfd);

    size_t encode_size = DIV(4 * size, 3);

    buffer_t b64;
    buffer_setup(&b64, safe_malloc(encode_size), encode_size);

    info("encoding...\n");

    TINI(0);
    base64_encode(&b64, &rnd);
    TINI(1);

    buffer_t buf;
    buffer_setup(&buf, safe_malloc(size), size);

    info("decoding...\n");

    TINI(2);
    base64_decode(&buf, &b64);
    TINI(3);

    info("encode=%f decode=%f cmp=%i\n", TDIF(0, 1), TDIF(2, 3), byte_cmp(rnd.data, buf.data, size));

    return 0;
}
