#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <tuple>


// Definitions, global vars
typedef std::tuple<std::string, std::string, std::string> Date;
typedef std::tuple<Date, int, int, float> SalesRecord;
typedef std::map<std::string, float> MonthMap;
typedef std::map<int, float> StoreIDMap;

std::list<SalesRecord> buffer;
float global_total = 0;
MonthMap global_month_map;
StoreIDMap global_store_map;
int buffer_size, runs;
std::mutex producer_mtx, consumer_mtx;
std::condition_variable cv;

// Function prototypes
void producer(int);
void consumer(int);
SalesRecord pop_front();
int random_int(int, int);
float random_decimal(float, float);
void sleep(int);
Date random_date_tuple();
void display_results(std::string, MonthMap&, StoreIDMap&, float total);


// Functions

/***
 * Produce random store records
 ***/
void producer(int store_num){
    std::unique_lock<std::mutex> pro_lock(producer_mtx, std::defer_lock);
    while(runs > 0){
        pro_lock.lock();    // lock and enter critical section

        if(runs <= 0){
            pro_lock.unlock();
            break;}
        runs--;   // decrement

        // generate sales record
        SalesRecord record = std::make_tuple(
            random_date_tuple(),            // random date
            store_num,
            random_int(1, 6),               // random register id
            random_decimal(0.50, 999.99));  // random sale amount

        while(buffer.size() == buffer_size);     // pause production while buffer is full
        buffer.push_back(record);    // push sales record into buffer
        cv.notify_one();            // notify one consumer
        pro_lock.unlock();          // unlock and exit critical section
        sleep(random_int(5, 40));   // randomly sleep
    }
    cv.notify_all();    // signal all consumers that production has completed
}

/***
 * Consume ...
 ***/
void consumer(int id){
    float local_total = 0;
    MonthMap local_month_map;
    StoreIDMap local_store_map;
    std::unique_lock<std::mutex> con_lock(consumer_mtx, std::defer_lock);

    do{
        con_lock.lock();    // enter critical section
        cv.wait(con_lock);

        if(buffer.size() > 0 or runs > 0){
            SalesRecord sales_record = pop_front();
            Date date = std::get<0>(sales_record);
            int store_id = std::get<1>(sales_record);
            float sales_amount = std::get<3>(sales_record);

            // add month totals
            std::string month_key = std::get<1>(date) + std::string("-") + std::get<2>(date);  // MM-YY key
            local_month_map[month_key] += sales_amount;
            global_month_map[month_key] += sales_amount;

            // add total for store id
            local_store_map[store_id] += sales_amount;
            global_store_map[store_id] += sales_amount;

            // add to total
            local_total += sales_amount;
            global_total += sales_amount;
        }

        con_lock.unlock();  // exit critical section
    } while(buffer.size() > 0 or runs > 0);

    // print local consumer totals
    con_lock.lock();
    display_results(
        std::string("Consumer Thread ") + std::to_string(id),
        local_month_map,
        local_store_map,
        local_total);
    con_lock.unlock();
}

/***
 *
 ***/
void display_results(std::string title, MonthMap& month_map, StoreIDMap& store_map, float total){
    printf("%s:\n", title.c_str());
    printf("========================\n");
    printf("[ Store-Wide Total Sales ]\n");
    for(auto it = store_map.cbegin(); it != store_map.cend(); ++it){
        printf("Store ID %d: $%.2f\n", (*it).first, (*it).second);}

    printf("\n[ Month-Wise Total Sales ]\n");
    for(auto it = month_map.cbegin(); it != month_map.cend(); ++it){
        printf("%s: $%.2f\n", it->first.c_str(), it->second);}
    printf("\nTotal: $%.2f\n", total);
    printf("========================\n\n");
}

/***
 * Pop from front of the buffer
 ***/
SalesRecord pop_front(){
    SalesRecord sales_record = buffer.front();
    buffer.pop_front();
    return sales_record;}

/***
 * Generate random integer value
 ***/
int random_int(int min, int max){
    return rand() % max + min;}

/***
 * Generate random decimal value
 ***/
float random_decimal(float min, float max){
    // https://stackoverflow.com/questions/14638739/generating-a-random-double-between-a-range-of-values
    std::uniform_real_distribution<float> dist(min, max);
    std::mt19937 rng;
    rng.seed(std::random_device{}());
    return dist(rng);}

/***
 * Sleep for n number of milliseconds
 ***/
void sleep(int n){
    std::this_thread::sleep_for(std::chrono::milliseconds(n));}

/***
 * Generate a random (DD,MM,YY) tuple
 ***/
Date random_date_tuple(){
    int dd = random_int(1, 30);
    int mm = random_int(1, 12);
    std::string DD, MM;

    if(dd <= 9){
        DD = std::string("0") + std::to_string(dd);}
    else{
        DD = std::to_string(dd);}

    if(mm <= 9){
        MM = std::string("0") + std::to_string(mm);}
    else{
        MM = std::to_string(mm);}

    return std::make_tuple(DD, MM, "16");}


int main(int argc, char* argv[]){
    // assert the valid number of cli args are passed in
    if(argc < 4){
        // not enough cli args were provided, print help
        printf(
            "%s\n%s\n",
            "Usage:\tprodcon.bin <int:producers> <int:consumers> <int:buffersize> <(optional) int:runs>",
            "Example:\tprodcon.bin 4 3 10");
        exit(-1);}

    // cli args
    int p, c;
    p = std::stoi(argv[1]);     // number of producers
    c = std::stoi(argv[2]);     // number of consumers
    buffer_size = std::stoi(argv[3]);
    if(argc > 4){
        runs = std::stoi(argv[4]);}   // optional number of runs
    else{
        // default number of runs
        runs = 10000;}

    // validate cli args
    if(!(p > 0 and c > 0 and buffer_size > 0 and runs > 0)){
        printf("Args must be greater than 0\n");
        exit(-1);}
    printf("p: %d\nc: %d\nbuffer_size: %d\nruns: %d\n\n", p, c, buffer_size, runs);

    // consumers
    std::thread consumer_threads[c];
    for(int i = 0; i < c; i++){
        consumer_threads[i] = std::thread(consumer, i+1);}

    // producers
    std::thread producer_threads[p];
    for(int i = 0; i < p; i++){
        producer_threads[i] = std::thread(producer, i+1);}

    // join producers
    for(int i = 0; i < p; i++){
        producer_threads[i].join();}

    // join consumers
    for(int i = 0; i < c; i++){
        consumer_threads[i].join();}

    // display global results
    display_results(
        std::string("Global Results"),
        global_month_map,
        global_store_map,
        global_total
    );
}

