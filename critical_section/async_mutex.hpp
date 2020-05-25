#ifndef GODBOLT_COMPATIBLE
#pragma once
#endif // GODBOLT_COMPATIBLE

#include <mutex>

struct handle_base
{
    handle_base* prev = nullptr;
    handle_base* next = nullptr;
    
    virtual void run() && = 0;
};

class async_mutex
{
public:
   async_mutex() : head(nullptr), tail(nullptr)
   {}
    
   async_mutex(async_mutex&&) = delete;
    
   bool enqueue(handle_base* op)
   {
      std::lock_guard<std::mutex> lock(m);
      if (head == nullptr)
      {
          head = tail = op;
          return true;
      }
      
      tail->next = op;
      op->prev = tail;
      tail = op;
      return false;
   }
    
   handle_base* deque(handle_base* op)
   {
       std::lock_guard<std::mutex> lock(m);
       {
         auto& prevNext = (head == op) ? head : op->prev->next;
         prevNext = op->next;
       }
       
       {
         auto& nextPrev = (tail == op) ? tail : op->next->prev;
         nextPrev = op->prev;
       }

       return head;
   }

private:
   std::mutex m; 
   handle_base* head;
   handle_base* tail;
};
