#include "data_structures.h"
#include <stdexcept>
#include <cstdlib>

//Generic Linked List class - singly linked for now.
Node::Node(void* data_ptr) 
{
	//Guard 1 - Node has contain a valid pointer
	if (data_ptr == nullptr) 
	{
		throw std::invalid_argument("Node received null pointer");
	}

	this->data = data_ptr;
	this->next = nullptr;
}
//Get the next node in the list. 
Node* Node::get_next() 
{
	return this->next;
}
Node* Node::set_next(Node* next_node)
{
	this->next = next_node;
	return this;
}
//Just keep the pointer to the data, don't store all of it.
Node* Node::set_data(void* new_data)
{
	//Guard 1 - Node has contain a valid pointer
	if (new_data == nullptr)
	{
		throw std::invalid_argument("Node received null pointer");
	}
	//TODO when setting data, reconfirm that the data's deleted too.
	this->data = new_data;
	return this;
}

//Return the void pointer to the data
void* Node::get_data()
{
	//TODO when setting data, reconfirm that the data's deleted too.
	return this->data;
}


Linked_list::Linked_list() {
	this->head = nullptr;
	this->tail = nullptr;
}

//Add a node to the linked list
// -Take a dataPtr
// - Add new_now to the head of the list.
bool Linked_list::add_node(void* dataptr) {
	//Create a node to put the data_ptr in
	try 
	{
		//Create a new Node class with the data loaded in. 
		Node* new_node = (Node *) malloc(sizeof(class Node));
		new (new_node) Node(dataptr);
		//Case 1: Empty List - if it breaks add a check for tail nullness
		//  head->new_node<-tail
		//           \->nullptr
		if (!this->head) 
		{
			this->head = new_node;
			this->tail = new_node;
		} else if (this->get_head() == this->get_tail()) {
			//Case 2: 1 Node List
			// head isn't nullptr and they are the same pointer. 
			//new node points at the head 
			new_node->set_next(this->get_head());
			//The new node becomes the head.
			this->head = new_node;
		}
		//Case 3: >2 Node List
		else {
			new_node->set_next(this->get_head());
			this->head = new_node;
		}
	}
	catch (int e)
	{
		return false;
	}
	return true;
}

Node* Linked_list::get_head() {
	return this->head;
}

Node* Linked_list::get_tail() {
	return this->tail;
}


bool Linked_list::remove_at(unsigned int index) {
	Node* curr_node = this->head;
	bool removed = false;

	//Advanced until the current node points at the specificed index
	for (int i = 0; curr_node && i < index; i++) {
		curr_node->set_next(curr_node->get_next());
	}
	
	//If it gets here the next node is the index specificed
	if (curr_node) {
		if (curr_node == this->head) {
			this->head = curr_node->get_next();
			curr_node->set_next(nullptr);
			removed = true;
		}
		//If the data is the tail even in a single or multi ele list. it'll handle the tail==head case
		if (curr_node == this->tail) {
			this->tail = curr_node->get_next();
			curr_node->set_next(nullptr);
			removed = true;
		}
		
		//If nothing has been removed then its a multi dimenstional list, feel free to go ham.
		if (!removed) {
			//Remove it from the list - guaranteed to be a multi element list so this should be fine. 
			curr_node->set_next(curr_node->get_next()->get_next());
		}
		return true;
	}
	//List bottomed before reaching index
	else {
		return false;
	}
}


