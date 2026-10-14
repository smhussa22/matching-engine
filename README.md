# C++ Low-Latency Matching Engine w/ Limit Order Book

A limit order book matching engine, written using modern C++23, cache-friendly data structures, and my own personal networking library [maroof.net](https://github.com/smhussa22/maroof-net).

The repository is heavily commented in layman's terms as much as possible, as I too was learning about matching engines and order books while creating this project. However for those who don't want to scrape the code, a brief summary is available below.

# How It Works (Brief Summary)

# Benchmarks

# Repositories That Helped

### [`smhussa22/maroof-net`](https://github.com/smhussa22/maroof-net)

My own networking library that implements reliable-UDP. 

### [`atribout/limit-order-book`](https://github.com/atribout/limit-order-book)

A C++ limit order book with tons of modern C++ and optimization techniques, written by a quantitative developer himself.

This order book is pretty simple, supporting only Good Till Cancelled orders, but helped me out a lot with its use of modern C++ and cache-friendly data structures.

# Optimization Techniques Used

## `OrderBook`