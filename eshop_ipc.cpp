#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define NUM_PRODUCTS 20
#define NUM_CUSTOMERS 5
#define NUM_ORDERS_PER_CUSTOMER 10
#define PROCESSING_TIME 1     /* seconds */
#define INITIAL_STOCK 2

typedef struct {
    char description[50];
    float price;
    int stock;
    int total_requests;
    int successful_orders;
    int failed_orders;
} Product;

/* Global product catalog */
Product catalog[NUM_PRODUCTS];

/* Initialize catalog with demo data */
void initialize_catalog(void) {
    int i;
    srand((unsigned int)time(NULL));

    for (i = 0; i < NUM_PRODUCTS; i++) {
        sprintf(catalog[i].description, "Product %d", i + 1);
        catalog[i].price = (float)(10 + rand() % 91); /* 10–100 */
        catalog[i].stock = INITIAL_STOCK;
        catalog[i].total_requests = 0;
        catalog[i].successful_orders = 0;
        catalog[i].failed_orders = 0;
    }
}

/* Process a single order in the parent (e-shop) */
void process_order(int customer_id, int product_index, int *success, float *total_cost) {
    Product *p;

    if (product_index < 0 || product_index >= NUM_PRODUCTS) {
        fprintf(stderr, "Invalid product index received: %d\n", product_index);
        *success = 0;
        *total_cost = 0.0f;
        return;
    }

    p = &catalog[product_index];
    p->total_requests++;

    if (p->stock > 0) {
        p->stock--;
        p->successful_orders++;
        *success = 1;
        *total_cost = p->price;
        printf("[SHOP] Customer %d: order SUCCESS for %s (%.2f). Stock left: %d\n",
               customer_id, p->description, *total_cost, p->stock);
    } else {
        p->failed_orders++;
        *success = 0;
        *total_cost = 0.0f;
        printf("[SHOP] Customer %d: order FAILED for %s (out of stock).\n",
               customer_id, p->description);
    }
}

/*
   IPC design:

   For each customer i we create two pipes:
   - customer_to_shop[i][2]: customer writes requests (product index) to shop
       - customer writes to fd [1]
       - shop reads   from fd [0]

   - shop_to_customer[i][2]: shop sends response (success + cost) back
       - shop writes to fd [1]
       - customer reads from fd [0]
*/

int main(void) {
    int customer_to_shop[NUM_CUSTOMERS][2];
    int shop_to_customer[NUM_CUSTOMERS][2];
    pid_t pids[NUM_CUSTOMERS];
    int i, j;

    initialize_catalog();

    /* Create pipes and fork children (customers) */
    for (i = 0; i < NUM_CUSTOMERS; i++) {
        if (pipe(customer_to_shop[i]) == -1) {
            perror("pipe customer_to_shop");
            exit(EXIT_FAILURE);
        }
        if (pipe(shop_to_customer[i]) == -1) {
            perror("pipe shop_to_customer");
            exit(EXIT_FAILURE);
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) {
            /* === CHILD PROCESS: Customer i === */
            int customer_id = i + 1;
            int product_index;
            int success;
            float total_cost;
            unsigned int seed;

            /* Close unused ends of ALL pipes for this child */
            for (j = 0; j < NUM_CUSTOMERS; j++) {
                /* child reads from shop_to_customer[i][0] only */
                if (j == i) {
                    close(shop_to_customer[j][1]);        /* close write end (shop side) */
                    close(customer_to_shop[j][0]);        /* close read end (shop side) */
                } else {
                    /* completely close other customers' pipes */
                    close(shop_to_customer[j][0]);
                    close(shop_to_customer[j][1]);
                    close(customer_to_shop[j][0]);
                    close(customer_to_shop[j][1]);
                }
            }

            /* Seed RNG per child so each gets different sequence */
            seed = (unsigned int)(time(NULL) ^ getpid());
            srand(seed);

            printf("[CUSTOMER %d] Started.\n", customer_id);

            for (j = 0; j < NUM_ORDERS_PER_CUSTOMER; j++) {
                product_index = rand() % NUM_PRODUCTS;

                /* Send order request to shop */
                if (write(customer_to_shop[i][1], &product_index, sizeof(int)) != sizeof(int)) {
                    perror("write to shop");
                    break;
                }

                /* Wait for response from shop */
                if (read(shop_to_customer[i][0], &success, sizeof(int)) != sizeof(int)) {
                    perror("read success from shop");
                    break;
                }
                if (read(shop_to_customer[i][0], &total_cost, sizeof(float)) != sizeof(float)) {
                    perror("read cost from shop");
                    break;
                }

                if (success) {
                    printf("[CUSTOMER %d] Order %d: SUCCESS (product %d, cost %.2f)\n",
                           customer_id, j + 1, product_index + 1, total_cost);
                } else {
                    printf("[CUSTOMER %d] Order %d: FAILED (product %d)\n",
                           customer_id, j + 1, product_index + 1);
                }

                sleep(PROCESSING_TIME);
            }

            close(customer_to_shop[i][1]);
            close(shop_to_customer[i][0]);
            printf("[CUSTOMER %d] Finished.\n", customer_id);
            exit(EXIT_SUCCESS);
        }

        /* === PARENT: just continue loop to spawn next child === */
    }

    /* === PARENT PROCESS: e-shop === */

    /* Close the ends not used by the shop */
    for (i = 0; i < NUM_CUSTOMERS; i++) {
        /* Shop reads from customer_to_shop[i][0], writes to shop_to_customer[i][1] */
        close(customer_to_shop[i][1]);   /* close write end (child side) */
        close(shop_to_customer[i][0]);   /* close read end (child side) */
    }

    printf("\n[SHOP] All customers started. Processing orders...\n");

    /* For each customer, receive NUM_ORDERS_PER_CUSTOMER requests */
    for (i = 0; i < NUM_CUSTOMERS; i++) {
        int customer_id = i + 1;
        for (j = 0; j < NUM_ORDERS_PER_CUSTOMER; j++) {
            int product_index;
            int success;
            float total_cost;

            if (read(customer_to_shop[i][0], &product_index, sizeof(int)) != sizeof(int)) {
                /* If child closed pipe early */
                break;
            }

            process_order(customer_id, product_index, &success, &total_cost);

            if (write(shop_to_customer[i][1], &success, sizeof(int)) != sizeof(int)) {
                perror("write success to customer");
                break;
            }
            if (write(shop_to_customer[i][1], &total_cost, sizeof(float)) != sizeof(float)) {
                perror("write cost to customer");
                break;
            }

            /* Simulate processing time on shop side as well (optional) */
            /* sleep(PROCESSING_TIME); */
        }
    }

    /* Close shop pipe ends */
    for (i = 0; i < NUM_CUSTOMERS; i++) {
        close(customer_to_shop[i][0]);
        close(shop_to_customer[i][1]);
    }

    /* Wait for all children */
    for (i = 0; i < NUM_CUSTOMERS; i++) {
        waitpid(pids[i], NULL, 0);
    }

    /* Compute statistics */
    {
        int total_requests = 0;
        int total_success = 0;
        int total_failed = 0;
        float total_revenue = 0.0f;

        int k;
        printf("\n[SHOP] Per-product statistics:\n");
        for (k = 0; k < NUM_PRODUCTS; k++) {
            Product *p = &catalog[k];
            total_requests += p->total_requests;
            total_success  += p->successful_orders;
            total_failed   += p->failed_orders;
            total_revenue  += p->successful_orders * p->price;

            printf("  %s | price: %.2f | stock: %d | requests: %d | success: %d | failed: %d\n",
                   p->description, p->price, p->stock,
                   p->total_requests, p->successful_orders, p->failed_orders);
        }

        printf("\n[SHOP] Overall statistics:\n");
        printf("  Total requests:          %d\n", total_requests);
        printf("  Total successful orders: %d\n", total_success);
        printf("  Total failed orders:     %d\n", total_failed);
        printf("  Total revenue:           %.2f\n", total_revenue);
    }

    printf("\n[SHOP] Simulation finished.\n");
    return 0;
}

