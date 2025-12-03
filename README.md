# E-Shop IPC Simulation (C, Processes & Pipes)

This project simulates a simple e-shop using multiple processes and UNIX pipes.  
The **parent process** acts as the shop, while each **child process** represents a customer that sends orders over inter-process communication (IPC).

It is designed as a teaching / portfolio example for **Operating Systems**, **processes**, and **IPC with pipes** in C.

---

## 🧩 Features

- `NUM_CUSTOMERS` concurrent customer processes (via `fork()`)
- `NUM_PRODUCTS` product catalog (description, price, stock, stats)
- Each customer sends `NUM_ORDERS_PER_CUSTOMER` random orders
- Two unidirectional pipes per customer:
  - `customer_to_shop`: order requests (product index)
  - `shop_to_customer`: order responses (success + cost)
- Shop:
  - checks stock and updates product statistics
  - simulates processing time
  - logs each order (success / failure)
- Final statistics:
  - per-product stock, requests, successes, failures
  - total revenue, total successful / failed orders

---

## 🛠️ Build & Run

This project targets **Linux / UNIX** environments (uses `fork()`, `pipe()`, `unistd.h`).

```bash
gcc eshop_ipc.c -o eshop_ipc
./eshop_ipc
```
🧱 IPC Design
For each customer i we create two pipes:

customer_to_shop[i][2]

child writes product index → fd[1]

parent reads product index ← fd[0]

shop_to_customer[i][2]

parent writes response (success + cost) → fd[1]

child reads response ← fd[0]

This keeps the communication clean and directional, and isolates each customer's traffic.

📦 Data Structures
```c
typedef struct {
    char  description[50];
    float price;
    int   stock;
    int   total_requests;
    int   successful_orders;
    int   failed_orders;
} Product;
```
The catalog is a global array of Product:

```c
Product catalog[NUM_PRODUCTS];
```
The shop updates this catalog whenever an order is processed.

🎓 Concepts Demonstrated
fork() and process creation

One-way and two-way communication with pipe()

Blocking read() / write() semantics

Basic synchronization via pipes (request/response pattern)

Randomized load via rand()

Global shared state in the parent process

Per-product statistics & final reporting
