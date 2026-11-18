proc redisbenchmark_tls_config {testsdir} {
    set tlsdir [file join $testsdir tls]
    set cert [file join $tlsdir client.crt]
    set key [file join $tlsdir client.key]
    set cacert [file join $tlsdir ca.crt]

    if {$::tls} {
        return [list --tls --cert $cert --key $key --cacert $cacert]
    } else {
        return {}
    }
}

proc redis_benchmark_bin {} {
    # The CMake tree ships redis-benchmark.exe next to the server.
    if {$::tcl_platform(platform) eq "windows"} {
        return [file nativename [file join [file dirname [redis_server_bin]] redis-benchmark.exe]]
    }
    return src/redis-benchmark
}

proc redisbenchmark {host port {opts {}}} {
    set cmd [list [redis_benchmark_bin] -h $host -p $port]
    lappend cmd {*}[redisbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc redisbenchmarkuri {host port {opts {}}} {
    set cmd [list [redis_benchmark_bin] -u redis://$host:$port]
    lappend cmd {*}[redisbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc redisbenchmarkuriuserpass {host port user pass {opts {}}} {
    set cmd [list [redis_benchmark_bin] -u redis://$user:$pass@$host:$port]
    lappend cmd {*}[redisbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}
