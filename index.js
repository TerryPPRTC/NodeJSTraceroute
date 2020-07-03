// Use the "bindings" package to locate the native bindings.
const traceroute = require('bindings')('traceroute');

// Call the function "startWork" which the native bindings library exposes.
// The function returns a promise which will be resolved at the complete of the
// work with a json string of worked out primes. This resolution simply prints them out.
traceroute.startWork("www.kktv5.com")
  .then((thePrimes) => {
    console.log("Received primes from completed work: " + thePrimes)
  });
