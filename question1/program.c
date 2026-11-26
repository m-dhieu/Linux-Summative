#include <stdio.h>
#include <stdlib.h>
#define DEPOSIT 'D'
#define WITHDRAWAL 'W'
#define NUM_TRANSACTIONS 5

double initial_balance = 1000.00;

typedef struct {
    int id;
    double amount;
    char type;
} Transaction;

void initialize_transactions(Transaction *transactions, int count) {
    for (int i = 0; i < count; i++) {
        transactions[i].id = 1001 + i;
        transactions[i].amount = (i + 1) * 100.0;
        transactions[i].type = (i % 2 == 0) ? DEPOSIT : WITHDRAWAL;
    }
}

/* process & calculate totals */
void process_transactions(const Transaction *transactions, int count,
                          double *total_deposits, double *total_withdrawals) {
    *total_deposits = 0.0;
    *total_withdrawals = 0.0;

    for (int i = 0; i < count; i++) {
        if (transactions[i].type == DEPOSIT) {
            *total_deposits += transactions[i].amount;
        } else if (transactions[i].type == WITHDRAWAL) {
            *total_withdrawals += transactions[i].amount;
        }
    }
}

void display_summary(double total_deposits, double total_withdrawals) {
    double final_balance = initial_balance + total_deposits - total_withdrawals;
    printf("\nBANK TRANSACTION SUMMARY\n\n");
    printf("Initial Balance:   $%.2f\n", initial_balance);
    printf("Total Deposits:    $%.2f\n", total_deposits);
    printf("Total Withdrawals: $%.2f\n", total_withdrawals);
    printf("Final Balance:     $%.2f\n", final_balance);
}

int main(void) {
    double total_deposits = 0.0;
    double total_withdrawals = 0.0;
    Transaction *transactions = malloc(NUM_TRANSACTIONS * sizeof(Transaction));
    
    if (transactions == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    initialize_transactions(transactions, NUM_TRANSACTIONS);
    
    process_transactions(transactions, NUM_TRANSACTIONS,
                         &total_deposits, &total_withdrawals);
    
    display_summary(total_deposits, total_withdrawals);

    free(transactions);

    return 0;
}

