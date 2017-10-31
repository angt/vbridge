function hex (str)
{
    tmp = toupper(substr(str, 3, 8))
    return substr("00000000", 1, 8-length(tmp)) tmp
}

{
    if (NF<5 || $1!="#define" || substr($5, 1, 2)!="U+")
        next

    key = hex($3)

    if (key<="000000FF" || (key>="01000100" && key<="0110FFFF"))
        next

    ucs = hex($5)
    vec[ucs] = key
}

END {
    print "static struct {"
    print "    unsigned ucs;"
    print "    unsigned keysym;"
    print "} keysyms[] = {"

    min = "00000000"

    for (i in vec) {
        max = "FFFFFFFF"
        for (j in vec)
            if (min<j && j<max)
                max = j
        min = max
        print "    { 0x" min ", 0x" vec[min] " },"
    }

    print "};"
}
