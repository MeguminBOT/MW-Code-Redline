/**
 * The front-end engine's lists (original: zFEng): FEMinList, doubly linked with a count (a node's next at +4 and previous at +8), and the lookups
 * that walk them. A removed node's links are marked, as in the original, so that a use after it stands out.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
struct Node
{
    void *vtable;
    Node *next;
    Node *previous;
};

struct List
{
    void *vtable;
    unsigned int count;
    Node *head;
    Node *tail;
};

Node *const kRemoved = reinterpret_cast<Node *>(0xabadcafe);

inline void Insert(List &list, Node *after, Node *node)
{
    if (!node)
        return;
    if (!after)
    {
        node->next = list.head;
        if (list.head)
            list.head->previous = node;
        node->previous = nullptr;
        list.head = node;
    }
    else
    {
        node->next = after->next;
        if (after->next)
            after->next->previous = node;
        node->previous = after;
        after->next = node;
    }
    if (list.tail == after)
        list.tail = node;
    ++list.count;
}
} // namespace

void FEMinList::AddNode(FEMinNode *after, FEMinNode *node)
{
    Insert(*reinterpret_cast<List *>(this), reinterpret_cast<Node *>(after), reinterpret_cast<Node *>(node));
}

void FEMinList::AddHead(FEMinNode *nodeObject)
{
    List &list = *reinterpret_cast<List *>(this);
    Node *node = reinterpret_cast<Node *>(nodeObject);
    if (!node)
        return;
    node->next = list.head;
    if (list.head)
        list.head->previous = node;
    node->previous = nullptr;
    list.head = node;
    if (!list.tail)
        list.tail = node;
    ++list.count;
}

void FEMinList::AddTail(FEMinNode *node)
{
    List &list = *reinterpret_cast<List *>(this);
    Insert(list, list.tail, reinterpret_cast<Node *>(node));
}

FEMinNode *FEMinList::RemNode(FEMinNode *nodeObject)
{
    List &list = *reinterpret_cast<List *>(this);
    Node *node = reinterpret_cast<Node *>(nodeObject);
    if (!node)
        return nodeObject;
    if (node == list.head)
        list.head = node->next;
    if (node == list.tail)
        list.tail = node->previous;
    if (node->previous)
        node->previous->next = node->next;
    if (node->next)
        node->next->previous = node->previous;
    node->next = kRemoved;
    node->previous = kRemoved;
    --list.count;
    return nodeObject;
}

FEMinNode *FEMinList::RemHead()
{
    Node *head = reinterpret_cast<List *>(this)->head;
    if (head)
        RemNode(reinterpret_cast<FEMinNode *>(head));
    return reinterpret_cast<FEMinNode *>(head);
}

FEMinNode *FERefList::GetHead() const
{
    // A list that refers to another (the flag at +4) holds it where its head would be.
    const char *list = reinterpret_cast<const char *>(this);
    while (list[4])
        list = *reinterpret_cast<const char *const *>(list + 8);
    return *reinterpret_cast<FEMinNode *const *>(list + 8);
}

FEScript *FEObject::FindScript(unsigned int id) const
{
    FEScript *script = FirstScript;
    while (script && script->ID != id)
        script = script->Next;
    return script;
}

FEMessageResponse *FEObject::FindResponse(unsigned int id) const
{
    const char *response = reinterpret_cast<const char *>(FirstResponse);
    while (response && *reinterpret_cast<const unsigned int *>(response + 0xc) != id)
        response = *reinterpret_cast<const char *const *>(response + 4);
    return reinterpret_cast<FEMessageResponse *>(const_cast<char *>(response));
}
