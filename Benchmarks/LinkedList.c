#include <stdio.h>
#include <stdlib.h>

int numNodes = 0;

struct Node {
  int data;
  struct Node* next;
};

struct Node* create(const int data, struct Node* next) {
  struct Node* p = (struct Node*)malloc(sizeof(struct Node));
  p->data = data;
  p->next = next;
  numNodes++;
  return p;
}

struct Node* incAll(struct Node *head) {
  if (head) {
    return create(head->data + 1, incAll(head->next));
  } else {
    return NULL;
  }
}

int main(int argc, char** argv) {
  struct Node *n = NULL;
	struct Node *m = NULL;
	if(argc > 1){
  	for (int i = 0; i < argc; i++) {
    	n = create(i, n);
  	}
	}
	
	if(n){
  	m = incAll(n);
	}
  return 0;
}
