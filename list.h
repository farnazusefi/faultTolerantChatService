#include "fileService.h"

typedef struct Node_t 
{ 
    char mess[102400];
    int length;
    Node *next; 
} Node; 


void push(Node* head_ref, char *new_data, size_t data_size) 
{ 
  
    Node * current = head_ref;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = (Node *) malloc(sizeof(Node));
    /* now we can add a new variable */
    strcpy(current->next->mess, new_data);
    current->next->length = data_size;
    current->next->next = NULL;
}

int pop(Node ** head, char *returned_message, int *returned_length) {
    int retval = -1;
    Node *next_node = NULL;

    if (*head == NULL) {
        return -1;
    }

    next_node = (*head)->next;
    strcpy(returned_message, (*head)->mess);
    *returned_length = (*head)->length;
    free(*head);
    *head = next_node;

    return 1;
}