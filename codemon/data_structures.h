#ifndef MYHEADER_H
#define MYHEADER_H

//Generic Linked List class - singly linked for now.
class Node
{
private:
    void* data;     //Pointer to the data that's held.
    Node* next;     //Pointer to the next data node
public:
    //Constructor: Intended that the new node becomes the head of the list.
    Node(void* data_ptr);
    //Get the next node in the list.
    Node* get_next();
    Node* set_next(Node* next_node);
    //Just keep the pointer to the data, don't store all of it.
    Node* set_data(void* new_data);
    void* get_data();
};

class Linked_list
{
public:
    Node* head;
    Node* tail;
    Linked_list();
    //Add a node to the linked list
    // -Take a dataPtr
    // - Add new_now to the head of the list.
    bool add_node(void* dataptr);
    bool remove_at(unsigned int index);
    Node* get_head();
    Node* get_tail();
};

#endif
