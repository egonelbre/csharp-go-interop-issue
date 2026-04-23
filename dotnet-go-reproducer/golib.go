package main

import "C"

// ping is a trivial cgo entry point. Each call from a non-Go thread
// triggers needm (acquire M + install sigaltstack) on entry and dropm
// (disable sigaltstack + release M) on return. Rapid calls from many
// pthreads create the sigaltstack lifecycle churn needed to hit the race.
//
//export ping
func ping() C.int { return 42 }

func main() {}
