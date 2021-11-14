/**
 * Author   : Harish Gontu, Sharath Chandra Nakka
 * Purpose  : Solution to csmc scheduling using multithreading (POSIX threads)
 * Language : C
 * */

#define _BSD_SOURCE
#define _XOPEN_SOURCE   600
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define PROGRAMMING_PERIOD 2000
#define TUTORING_PERIOD 200


/**
 * Student Structure Storing following details :
 *     - priority i.e num of visits to csmc
 *     - last visited timestamp to csmc
 *     - last tutor who helped this student
 *     - synchronization with tutor i.e lock to be in sync with tutor for whole tutoring period
 * */
struct student_t {
  int priority;
  int time;
  int tutor;
  sem_t being_tutored;
};

// Node chain for implementation of queue (both standard and priority variant)
struct node_t {
  int id; // id of student in line
  struct node_t* next; // next in line
};


int empty_chairs; // number of empty chairs in waiting area
int num_waiting;  // number of students waiting for tutor in waiting area
int in_session;   // number of students being tutored parallely at any point of time
int num_session;  // number of sessions given by a tutor
int num_requests; // number of requests accepted by the coordinator

struct node_t* waiting_line = NULL;  // standard FIFO queue to represent incoming students in waiting area
struct node_t* priority_line = NULL; // priority based FIFO queue to represent students selected to be tutored
                                     // Linked list implementations for both queue above

struct student_t *students;          // array of students
pthread_mutex_t warea;  // mutex lock for waiting area;
pthread_mutex_t tarea;  // mutex lock for tutoring area;
pthread_mutex_t csmc;   // mutex lock for csmc;
sem_t student_coordinator; // semaphore for student -> coordinator notification
sem_t coordinator_tutor;   // semaphore for coordinator -> tutor notification
sem_t tutor_coordinator;   // semaphore for tutor -> coordinator notification

int num_students, num_tutors, num_chairs, num_help;


/** 
 * @return the current time in seconds and microseconds;
 * */
int
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec;
}

/**
 * @brief  compare priorities between two students
 * @param  student id of two students
 * @return true if priority of student 1 is less than that of student 2 and false otherwise
 * */
int 
higher_priority(int s1, int s2) {
  int diff = students[s1].priority - students[s2].priority;
  if (diff == 0)
    diff = students[s1].time - students[s2].time;
  return diff < 0;
}

/**
 * @brief push a student in priority queue
 * @param student id
 * */
void 
priority_push(int id) {
  struct node_t *node = malloc(sizeof(struct node_t));
  node->id = id;
  node->next = NULL;
  if (priority_line) {
    // Non empty queue
    if (higher_priority(id, priority_line->id)) {
      // if student has higher priority than head , then he/her becomes the head of queue
      node->next = priority_line;
      priority_line = node;
    } else {
      // find the student who has lesser priority than given student and insert before that student
      for (struct node_t *second, *first = priority_line; first; first = first->next) {
        second = first->next;
        if (!second || higher_priority(id, second->id)) {
          node->next = second;
          first->next = node;
          break;
        }
      }
    }
  } else {
    // if line is empty , current student is the head
    priority_line = node;
  }
}

/**
 * @brief push a student in standard (fifo) queue
 * @param student id
 * */
void 
push(int id) {
  struct node_t *node = malloc(sizeof(struct node_t));
  node->id = id;
  node->next = NULL;
  if (waiting_line) {
    // append at end of queue if non-empty
    struct node_t *queue = waiting_line;
    while (queue->next)
      queue = queue->next;
    queue->next = node;
  } else {
    // head if queue is empty
    waiting_line = node;
  }
}
/**
 * @brief pop a student from priority queue
 * @return student id of highest priority i.e the head of queue
 * */
int 
priority_pop() {
  // return -1 if queue is empty
  if (!priority_line)
    return -1;
  // pop the head and fix the head pointer to next of head
  int result = priority_line->id;
  struct node_t *temp = priority_line;
  priority_line = priority_line->next;
  free(temp);
  return result;
}

/**
 * @brief pop a student from standard queue
 * @return student id of head
 * */
int 
pop() {
  // return -1 if queue is empty
  if (!waiting_line)
    return -1;
  // pop the head and fix the head pointer to next of head
  int result = waiting_line->id;
  struct node_t *temp = waiting_line;
  waiting_line = waiting_line->next;
  free(temp);
  return result;
}

/**
 * @brief  thread program for student
 * @param  id of the student
 * */
void*
student(void *id) {
  // sid -> student id
  int sid = *((int*) id);

  int h = num_help;

  // seed the random 
  srand(now());

  int period; 

  // For all help requests
  while (h != 0) {

    // programming phase of student
    // period between 0 to PROGRAMMING_PERIOD
    period = (rand() % (PROGRAMMING_PERIOD + 1)) ;
    usleep(period);

    // visit csmc for help , checkout the waiting area
    pthread_mutex_lock(&warea);

    // update priority of the student
    students[sid].priority++;
    students[sid].time = now();

    // search for an empty chair
    if (empty_chairs == 0) {
      printf("S: Student %d found no empty chair. Will try again later.\n", sid);
      pthread_mutex_unlock(&warea);
      continue;
    }

    // take an empty chair and wait for tutor
    empty_chairs--;
    push(sid);
    printf("S: Student %d takes a seat. Empty chairs = %d.\n", sid, empty_chairs);
    pthread_mutex_unlock(&warea);

    // notify coordinator using semaphore
    sem_post(&student_coordinator);

    // sleep for tutoring phase i.e sychronous wait till the tutoring period
    sem_wait(&students[sid].being_tutored);

    // finished  tutoring this received help
    printf("S: Student %d receive help from Tutor %d.\n", sid, students[sid].tutor);

    h--;
  }

  // Once all help requests complete, let csmc know that you are done :)
  pthread_mutex_lock(&csmc);
  num_students--;
  sem_post(&student_coordinator);
  pthread_mutex_unlock(&csmc);

  return NULL;
}

/**
 * @brief  thread program for coordinator
 * */
void*
coordinator(void *args) {
  int at_desk; // placeholder for student at coordinator (analogy like customer at receptionist desk)
  while (1) { 
    // if no more students  left  to tutor, 
    // coordinator notifies all tutors that csmc is free to close
    // then graceful terminates
    pthread_mutex_lock(&csmc);
    if (num_students == 0) {
      pthread_mutex_unlock(&csmc);
      int t;
      for (t= 0 ;t < num_tutors;t++)
        sem_post(&coordinator_tutor);
      break;
    }
    pthread_mutex_unlock(&csmc);

    // wait for a student needing help (with help of semaphore)
    sem_wait(&student_coordinator);

    // Organize the waiting student(s) in waiting area
    pthread_mutex_lock(&warea);

    // continue if no students waiting for help
    if (!waiting_line) {
      pthread_mutex_unlock(&warea);
      continue;
    }

    // for each student waiting in line at desk , push them into priority queue
    for (at_desk = pop(); at_desk != -1; at_desk = pop()) {
      priority_push(at_desk);

      // request for help from student registered at desk
      num_requests++;
      num_waiting++;
      printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", at_desk, students[at_desk].priority, num_waiting, num_requests);

      // notify the tutors about a student waiting for help
      sem_post(&coordinator_tutor);
    }

    pthread_mutex_unlock(&warea);

  }

  return NULL;
}

/**
 * @brief  thread program for tutor
 * @param  id of the tutor
 * */
void*
tutor(void *id) {
  // id of the tutor
  int tid = *((int*) id);

  while (1) {
    // if no more students  left  to tutor, 
    // terminate gracefully
    pthread_mutex_lock(&csmc);
    if (num_students == 0) {
      pthread_mutex_unlock(&csmc);
      break;
    }
    pthread_mutex_unlock(&csmc);

    // wait for coordinator to inform of any waiting students in priority queue
    sem_wait(&coordinator_tutor);

    // find student with highest priority
    int sid;
    pthread_mutex_lock(&warea);
    sid = priority_pop();
    if (sid == -1)
    {
      // Wait for next student if queue is empty
      pthread_mutex_unlock(&warea);
      continue;
    }

    // Student leaves chair from waiting area and moves to tutoring area
    num_waiting--;
    empty_chairs++;
    pthread_mutex_unlock(&warea);

    // start session between student and tutor
    pthread_mutex_lock(&tarea);
    in_session++;
    num_session++;
    pthread_mutex_unlock(&tarea);

    // help the student and release him
    students[sid].tutor = tid;
    usleep(TUTORING_PERIOD);
    sem_post(&students[sid].being_tutored);

    pthread_mutex_lock(&tarea);
    printf("T: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d.\n", sid, tid, in_session, num_session);
    in_session--;
    pthread_mutex_unlock(&tarea);

  }

  return NULL;
}


/**
 *  Main Thread
 * */
int 
main(int argc, char* argv[]) {
  if (argc != 5) {
    printf("ERROR : Exaclty need 4 arguments rather than %d\n", argc);
    return -1;
  }

  int i;
  void* value;

  num_students = atoi(argv[1]);
  num_tutors = atoi(argv[2]);
  num_chairs = atoi(argv[3]);
  num_help = atoi(argv[4]);

  if(num_students <= 0){
     printf("WARN : Number of students coming to csmc should be greater than 0\n");
  }

  if(num_help <= 0){
     printf("WARN : Number of help requests per student should be greater than 0\n");
  }
  
  if(num_tutors <= 0){
     printf("ERROR : Number of tutors should be greater than 0 in csmc\n");
     return -1;
  }
  
  if(num_chairs <= 0){
     printf("ERROR : Number of chairs should be greater than 0 in waiting area of csmc\n");
     return -1;
  }
  
  // Initially all chairs in waiting area are empty
  empty_chairs = num_chairs;

  // Initializing locks for csmc, waiting area and tutoring area
  assert(pthread_mutex_init(&csmc, NULL) == 0);
  assert(pthread_mutex_init(&warea, NULL) == 0);
  assert(pthread_mutex_init(&tarea, NULL) == 0);

  // Initializing semaphores for student -> coordinator , coordinator -> tutor, tutor -> coordinator
  sem_init(&student_coordinator, 0, 0);
  sem_init(&coordinator_tutor, 0, 0);
  sem_init(&tutor_coordinator, 0, 0);

  // Coordinator thread
  pthread_t coordinator_thread;
  assert(pthread_create(&coordinator_thread, NULL, coordinator, NULL) == 0);

  // Creating #num_tutor number of tutor threads
  int *tutor_id = malloc(sizeof(int) * num_tutors);
  pthread_t *tutor_threads = malloc(sizeof(pthread_t) * num_tutors);
  for(i = 0; i < num_tutors; i++) {
    tutor_id[i] = i;
    assert(pthread_create(&tutor_threads[i], NULL, tutor, (void *) &tutor_id[i]) == 0);
  }

  // Creating #num_student number of student threads
  int *student_id = malloc(sizeof(int) * num_students);
  students = malloc(sizeof(struct student_t) * num_students);
  pthread_t *student_threads = malloc(sizeof(pthread_t) * num_students);
  for(i = 0; i < num_students; i++) {
    student_id[i] = i;
    // Initializing the synchronization between student and tutor
    sem_init(&students[i].being_tutored, 0, 0);
    assert(pthread_create(&student_threads[i], NULL, student, (void *) &student_id[i]) == 0);
  }

  // Join all threads with main thread so process doesnt exit until they complete

  assert(pthread_join(coordinator_thread, &value) == 0);
  for(i = 0; i < num_students; i++) {
    assert(pthread_join(student_threads[i], &value) == 0);
  }
  for(i = 0; i < num_tutors; i++) {
    assert(pthread_join(tutor_threads[i], &value) == 0);
  }
  return 0;

}
