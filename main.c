/*
 //  main.c
 //  ctrader
 // Add Argument: ${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME} for terminal use
 //  Created by Mark Jayson Alvarez on 22/03/2018.
 //  Copyright © 2018 Mark Jayson Alvarez. All rights reserved.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <curl/curl.h>
#include <db.h>
#include <openssl/hmac.h>
#include <jansson.h>
#include <ncurses.h>

#define DEFAULT_HOMEDIR "./"
#define TRADESDB "trades.db"
/*****************************  STRUCTURES *****************************************/


struct RespData {
    char *memory;
    size_t size;
};


struct authdata{
    const char *id;
    const char *apikey;
    const char *secret_key;
    const char *id_apikey;
    const char *message;
    const char *signature;
};


struct prices{
    double highest_bid;
    double lowest_ask;
    double lock_bid_ask;
    double higher_bid_ask;
    double lower_bid_ask;
    double index_bid_ask;
    
};

struct order{
    double price;
    double amount;
    char order_id[11];
    char type[6];
    int placed;
};

typedef struct archive_dbs {
    DB *trades_dbp;
    const char *db_home_dir;
    char *trades_db_name;
} ARCHIVE_DBS;


typedef struct trade {
    char order_id[11];
    char time[25];
    char type[5];
    char profit[2];
    double price;
    double amount;
    double fee;
    double cost;
    
} TRADE;

/***************************** END STRUCTURES *****************************************/



/*****************************  FUNCTION PROTOTYPES ****************************/
char *itoa(long n, char s[]);
const char *make_signature(const char *message, const char *secret_key);
int kbhit(void);
char *reverse(char s[]);
void create_authdata(struct authdata *, char *);

void show_order_book(json_t *orders, const char *mytype, double myamount, double myprice, struct prices *, int price_index, int lock_index, double low, double high, double lastprice, TRADE last_trade);
void Getjson(struct RespData *, const char *url, char *post_params);
static size_t SaveRes(void *contents, size_t size, size_t nmemb, void *destination);
TRADE get_trades(ARCHIVE_DBS *archivedbs, char *nonce, char *request_params, char *timestamp, struct authdata* a, const char *, const char *, int last);

/************ BDB Database ***************/
void initialize_archivedbs(ARCHIVE_DBS *);
void set_db_filenames(ARCHIVE_DBS *my_archive);
int databases_setup(ARCHIVE_DBS *, const char *, FILE *);
int open_database(DB **, const char *, const char *,
                  FILE *);
int databases_close(ARCHIVE_DBS *);

int db_create(DB **dbp, DB_ENV *dbenv, u_int32_t flags);
int db_env_create(DB_ENV **dbenvp, u_int32_t flags);
/***************************** END FUNCTION PROTOTYPES *************************/





/***************************** GLOBAL VARIABLES ********************************/

json_t *open_orders_root, *cancel_orders_root, *account_balance_root, *archived_orders_root;


/***************************** END GLOBAL VARIABLES ****************************/





/***************************** FUNCTION DEFINITION *****************************/
/*------------------- initialize_archivedbs ----------------------------------------*/

void initialize_archivedbs(ARCHIVE_DBS *my_archive)
{
    my_archive->db_home_dir = DEFAULT_HOMEDIR;
    my_archive->trades_dbp = NULL;
    my_archive->trades_db_name = NULL;
}
/*------------------- end initialize_archivedbs ----------------------------------------*/

/*------------------- setup home directories ----------------------------------------*/

void set_db_filenames(ARCHIVE_DBS *my_archive)
{
    size_t size;
    
    /* Create the Trades DB file name */
    size = strlen(my_archive->db_home_dir) + strlen(TRADESDB) + 1;
    my_archive->trades_db_name = malloc(size);
    snprintf(my_archive->trades_db_name, size, "%s%s", my_archive->db_home_dir, TRADESDB);
}
/*------------------- end setup home directories ----------------------------------------*/


/*--------------------------------- open_database ----------------------------------------*/

/* Opens a database */

int open_database(DB **dbpp,       /* The DB handle that we are opening */
                  const char *file_name,     /* The file in which the db lives */
                  const char *program_name,  /* Name of the program calling this
                                              * function */
                  FILE *error_file_pointer)  /* File where we want error messages
                                              sent */
{
    DB *dbp;    /* For convenience */
    u_int32_t open_flags;
    int ret;
    
    /* Initialize the DB handle */
    ret = db_create(&dbp, NULL, 0);
    if (ret != 0) {
        fprintf(error_file_pointer, "%s: %s\n", program_name,
                db_strerror(ret));
        return(ret);
    }
    
    /* Point to the memory malloc'd by db_create() */
    *dbpp = dbp;
    
    /* Set up error handling for this database */
    dbp->set_errfile(dbp, error_file_pointer);
    dbp->set_errpfx(dbp, program_name);
    
    /* Set the open flags */
    open_flags = DB_CREATE;
    
    /* Now open the database */
    ret = dbp->open(dbp,        /* Pointer to the database */
                    NULL,       /* Txn pointer */
                    file_name,  /* File name */
                    NULL,       /* Logical db name (unneeded) */
                    DB_BTREE,   /* Database type (using btree) */
                    open_flags, /* Open flags */
                    0);         /* File mode. Using defaults */
    if (ret != 0) {
        dbp->err(dbp, ret, "Database '%s' open failed.", file_name);
        return(ret);
    }
    
    return (0);
}
/*--------------------------------- end open_database ----------------------------------------*/

/*--------------------------------- databases_setup ----------------------------------------*/
/* opens all databases */
int
databases_setup(ARCHIVE_DBS *my_archive, const char *program_name,
                FILE *error_file_pointer)
{
    int ret;
    
    /* Open the trades database */
    ret = open_database(&(my_archive->trades_dbp),
                        my_archive->trades_db_name,
                        program_name, error_file_pointer);
    if (ret != 0)
    /*
     * Error reporting is handled in open_database() so just return
     * the return code here.
     */
        return (ret);
    
    //printf("databases opened successfully\n");
    return (0);
}
/*--------------------------------- end databases_setup ----------------------------------------*/

/*---------------------------------  databases_close ----------------------------------------*/

/* Closes all the databases. */
int
databases_close(ARCHIVE_DBS *my_archive)
{
    int ret;
    /*
     * Note that closing a database automatically flushes its cached data
     * to disk, so no sync is required here.
     */
    
    if (my_archive->trades_dbp != NULL) {
        ret = my_archive->trades_dbp->close(my_archive->trades_dbp, 0);
        if (ret != 0)
            //fprintf(stderr, "Trades database close failed: %s\n",
            db_strerror(ret);
    }
    
    //printf("databases closed.\n");
    return (0);
}
/*--------------------------------- end databases_close ----------------------------------------*/


/*------------------- create_auth_data ----------------------------------------*/
void create_authdata(struct authdata *a, char *nonce){
    
    char message[1000];
    a->id = "";
    a->apikey = "";
    a->secret_key ="";
    a->id_apikey = "";
    strcpy(message, nonce);
    strcat(message, a->id_apikey);
    a->message = message;
    a->signature = make_signature(a->message, a->secret_key);
}
/*----------------end create_auth_data ----------------------------------------*/

/*--------------------------- make_signature ---------------------------------*/

const char *make_signature(const char *message, const char *secret_key){
    
    //HMAC-SHA256 Algorithm: http://www.askyb.com/cpp/openssl-hmac-hasing-example-in-cpp/
    char hmactemp[2][1];
    static char signature[65];
    memset(signature, 0, strlen(signature));
    unsigned char* result;
    unsigned int len = 20;
    result = (unsigned char*)malloc(sizeof(char) * len);
    HMAC_CTX ctx;
    HMAC_CTX_init(&ctx);
    HMAC_Init_ex(&ctx, secret_key, (unsigned int)(strlen(secret_key)), EVP_sha256(), NULL);
    HMAC_Update(&ctx, (unsigned char*)message, strlen(message));
    HMAC_Final(&ctx, result, &len);
    HMAC_CTX_cleanup(&ctx);
    for (int i = 0; i != len; i++){
        sprintf(hmactemp[i], "%02X", (unsigned int)result[i]);
        strcat(signature, hmactemp[i]);
    }
    free(result);
    return signature;
}
/*--------------------------- end make_signature ---------------------------------*/


/*------------------------------- SaveRes  ------------------------------------*/
// CURL WRITEFUNCTION - https://curl.haxx.se/libcurl/c/getinmemory.html
static size_t SaveRes(void *contents, size_t size, size_t nmemb, void *destination)
{
    size_t realsize = size * nmemb;
    struct RespData *mem = (struct RespData *)destination;
    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL){
        printw("not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size]=0;
    return realsize;
}
/*------------------------------- end SaveRes ---------------------------------*/


/*------------------------------- Getjson  ------------------------------------*/

void Getjson(struct RespData *chunk, const char *url, char *post_params){
    
    CURL *curl_handle;
    CURLcode res;
    struct curl_slist *list = NULL;
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, SaveRes);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    //curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
    
    /*
     curl_easy_setopt(curl_handle, CURLOPT_PROXY, "127.0.0.1:8888");
     curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
     curl_easy_setopt(curl_handle, CURLOPT_PROXYUSERPWD, "");*/
    
    if(post_params != NULL){
        list = curl_slist_append(list, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_params);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, list);
    }
    
    
    res = curl_easy_perform(curl_handle);
    curl_easy_cleanup(curl_handle);
    //printw(chunk->memory, "\n");
    //return(chunk.memory);
}
/*------------------------------- end Getjson ---------------------------------*/




/*------------------------------- Misc ---------------------------------*/

char *reverse(char s[])
{
    long c, i, j;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c; }
    return(s);
}
char *itoa(long n, char s[])
{
    int i;
    i = 0;
    do {
        s[i++]=n%10+'0';
    } while ((n /= 10) > 0);
    s[i] = '\0';
    
    return(reverse(s));
}

int kbhit(void)
{
    int ch = getch();
    
    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}
/*------------------------------- end Misc ---------------------------------*/


/*--------------------------- get_trades ---------------------------------*/

////////////////////////////////////////// RETRIEVE LAST ARCHIVED TRADE DATE FROM DATABASE ///////////////////////////////////////////
////////////////////////////////////////// DOWNLOAD ALL TRADES SINCE LAST TRADE DATE ////////////////////////////////////////////////

TRADE get_trades(ARCHIVE_DBS *archivedbs, char *nonce, char *request_params, char *timestamp, struct authdata* a, const char *mode, const char *trade_status, int last){
    
    TRADE trade;
    
    json_error_t error;
    int ret;
    
    if (strcmp(mode, "update") == 0){
        DBC *cursorp;
        DBT key, data;
        archivedbs->trades_dbp->cursor(archivedbs->trades_dbp, NULL, &cursorp, 0);
        memset(&key, 0, sizeof(DBT));
        memset(&data, 0, sizeof(DBT));
        memset(&trade, 0, sizeof(TRADE));
        key.data = &trade.order_id;
        key.size = sizeof(trade.order_id);
        data.data = &trade;
        data.ulen = sizeof(TRADE);
        data.flags = DB_DBT_USERMEM;
        ret = cursorp->get(cursorp, &key, &data, DB_PREV);
        char last_orderid[12];
        strcpy(last_orderid,trade.order_id);
        
        if (cursorp != NULL)
            cursorp->close(cursorp);
        
        
        
        // RETRIEVE ALL TRADE ARCHIVES
        
        //------------------------------------------------------------------------------------------------------------------
        struct RespData *traderesp = (void*)malloc(sizeof(struct RespData));
        char *archived_orders_json = malloc(strlen("{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"dateFrom\":\"%s\",\"lastTxDateFrom\":\"%s\",\"status\":\"%s\"}")+1);
        strcpy(archived_orders_json, "{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"dateFrom\":\"%s\",\"lastTxDateFrom\":\"%s\",\"status\":\"%s\"}");
        char *archived_orders_url = malloc(strlen("https://cex.io/api/archived_orders/BTC/USD/")+1);
        strcpy(archived_orders_url,"https://cex.io/api/archived_orders/BTC/USD/");
        
        memset(nonce, 0, strlen(nonce));
        memset(request_params, 0, strlen(request_params));
        strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
        a =  (void *)malloc(sizeof(struct authdata));
        create_authdata(a, nonce);
        sprintf(request_params, archived_orders_json, a->apikey, a->signature, nonce, trade.time, trade.time, trade_status);
        free(a);
        traderesp->memory = (void*)malloc(1);
        traderesp->size = 0;
        Getjson(traderesp, archived_orders_url, request_params);
        archived_orders_root = json_loads(traderesp->memory, 0, &error);
        //------------------------------------------------------------------------------------------------------------------
        
        // PREPARE DATABASE FOR WRITING
        
        memset(&key, 0, sizeof(DBT));
        memset(&data, 0, sizeof(DBT));
        
        key.data = &(trade.order_id);
        key.size = sizeof(long);
        data.data = &trade;
        data.size = sizeof(TRADE);
        
        int updated = 0;
        json_t *toporder;
        toporder = json_array_get(archived_orders_root, 0);
        if(strcmp(last_orderid,json_string_value(json_object_get(toporder, "orderId"))) == 0)
            updated = 1;
        
        for(long i = json_array_size(archived_orders_root) - 1; i >= 0; i--){
            //for(long i = 0; i < json_array_size(archived_orders_root); i++){
            
            json_t *order;
            order = json_array_get(archived_orders_root, i);
            double oldcost = trade.cost;
            double oldamount = trade.amount;
            
            if(updated) // STOP IF LAST ORDER IN DB == LAST TRADE
                break;
            
            memset(&trade, 0, sizeof(TRADE));
            strcpy(trade.order_id,json_string_value(json_object_get(order, "orderId")));
            strcpy(trade.time, json_string_value(json_object_get(order, "lastTxTime")));
            strcpy(trade.type, json_string_value(json_object_get(order, "type")));
            trade.amount = atof(json_string_value(json_object_get(order, "amount")));
            if(json_string_value(json_object_get(order, "price"))){
                trade.price = atof(json_string_value(json_object_get(order, "price")));
            }
            if(json_string_value(json_object_get(order, "tfa:USD"))){
                trade.fee = json_real_value(json_object_get(order, "tfa:USD"));
            }else{
                trade.fee = atof(json_string_value(json_object_get(order, "fa:USD")));
            }
            if(json_string_value(json_object_get(order, "tta:USD"))){
                trade.cost = atof(json_string_value(json_object_get(order, "tta:USD")));
            }else{
                trade.cost = atof(json_string_value(json_object_get(order, "ta:USD")));
            }
            //if ((trade.cost < oldcost && strcmp(trade.type, "buy")==0) || (trade.cost > oldcost && strcmp(trade.type, "sell")==0)){
            if ((trade.cost > oldcost && strcmp(trade.type, "sell")==0)||(trade.amount > oldamount && strcmp(trade.type, "buy")==0)){
                strcpy(trade.profit, "y");
            }else{
                strcpy(trade.profit, "n");
            }
            archivedbs->trades_dbp->put(archivedbs->trades_dbp, NULL, &key, &data, DB_NOOVERWRITE);
            
        }
        
        json_decref(archived_orders_root);
        free(traderesp->memory);
        free(traderesp);
        
        if(archived_orders_json != NULL)
            free(archived_orders_json);
        if(archived_orders_url != NULL)
            free(archived_orders_url);
    }else{
        DBC *cursorp;
        DBT key, data;
        int max = 0;
        archivedbs->trades_dbp->cursor(archivedbs->trades_dbp, NULL, &cursorp, 0);
        memset(&key, 0, sizeof(DBT));
        memset(&data, 0, sizeof(DBT));
        memset(&trade, 0, sizeof(TRADE));
        
        key.data = &trade.order_id;
        key.size = sizeof(trade.order_id);
        data.data = &trade;
        data.ulen = sizeof(TRADE);
        data.flags = DB_DBT_USERMEM;
        if(!last)
            printw("\n\n\n\n\n\n\n\n\n\n\tDate\t\tFee\tAmount\t    Price\tCost\t  Type\n");
        while ((ret = cursorp->get(cursorp, &key, &data, DB_PREV)) == 0){
            
            max++;
            if(max == 1 && last==1){
                if (cursorp != NULL)
                    cursorp->close(cursorp);
                return(trade);
            }
            
            if(max == 40){
                max = 0;
                break;
            }
            
            struct tm tm = {0};
            //2018-04-14T12:32:44.047Z
            strptime(trade.time, "%Y-%m-%dT%H:%M:%S", &tm);
            char wordtime[21];
            strftime (wordtime,80,"%b %d, %Y %I:%M%p",&tm);
            if (strcmp(trade.profit, "y") == 0){
                attron(COLOR_PAIR(2));
            }else{
                attron(COLOR_PAIR(3));
            }
            printw("%s\t%.2f\t%f    %4.2f\t%4.2f\t  %-4s\n", wordtime, trade.fee, trade.amount, trade.price, trade.cost, trade.type, trade.profit);
            
            
            refresh();
            memset(&key, 0, sizeof(DBT));
            memset(&data, 0, sizeof(DBT));
            memset(&trade, 0, sizeof(TRADE));
            key.data = &trade.order_id;
            key.size = sizeof(trade.order_id);
            data.data = &trade;
            data.ulen = sizeof(TRADE);
            data.flags = DB_DBT_USERMEM;
        }
        
        if (cursorp != NULL)
            cursorp->close(cursorp);
    }
    
    
    
    
    return(trade);
    
}
/*--------------------------- end get_trades ---------------------------------*/


/*--------------------------- show_order_book ---------------------------------*/

void show_order_book(json_t *orders, const char *mytype, double myamount, double myprice, struct prices *adj_price, int price_index, int lock_index, double low, double high, double lastprice, TRADE last_trade){
    json_t *bids, *asks, *ask_pair, *bid_pair, *lock_bid_pair, *lock_ask_pair;
    double ask_price, bid_price, ask_btc, bid_btc, bid_btc_total, ask_btc_total;
    int lock_bid_price, lock_ask_price;
    int maxorder = 10;
    
    
    
    
    
    printw("\n\n\t    LIVE ORDER BOOK\n\t       --CEX.io--\n\n");
    
    if(mytype && strcmp(mytype, "sell") == 0){
        printw("   last: %c - %f @ %.2f = %.2f\n", toupper(last_trade.type[0]), last_trade.amount, last_trade.price, last_trade.cost);
        
        printw("pending: %c - %f @ %.2f = %.2f <-\n\n\n", toupper(mytype[0]), myamount, myprice, (myprice * myamount) - (.0026 * (myprice * myamount)));
    }else if(mytype && strcmp(mytype, "buy") == 0){
        printw("   last: %c - %f @ %.2f = %.2f\n", toupper(last_trade.type[0]), last_trade.amount, last_trade.price, last_trade.cost);
        
        printw("pending: %c - %f @ %.2f = %.2f <-\n\n\n", toupper(mytype[0]), myamount, myprice, (myamount * myprice) + (.0026 * (myprice * myamount)));
    }else{
        printw("\n");
    }
    printw("\t\t  (AUTO)\n");
    printw("\tL:%4.2f ------- %4.2f:H\n\n", low, high);
    printw("\t      --> %4.2f <--\n\n", lastprice);
    //printw("\t\t     |\n");
    
    bids = json_object_get(orders, "bids");
    asks = json_object_get(orders, "asks");
    
    bid_btc_total = ask_btc_total = 0;
    //for(int i = 0; i < json_array_size(asks); i++){
    for(int i = 0; i < maxorder; i++){
        
        bid_pair = json_array_get(bids, i);
        ask_pair = json_array_get(asks, i);
        ask_btc_total += json_real_value(json_array_get(ask_pair, 1));
        bid_btc_total += json_real_value(json_array_get(bid_pair, 1));
    }
    
    
    printw("\t   BIDS              ASKS\nVol/%d: (%f)  %.0f%%  (%f)\n\n", maxorder, bid_btc_total,(ask_btc_total / bid_btc_total)*100, ask_btc_total);
    
    
    // STORE HIGHEST BID AND LOWEST ASK PRICE
    //if(mytype && (strcmp(mytype, "buy") == 0)){ //adj_price->highest_bid/lowest_ask contains highest bid/lowest ask/
    adj_price->highest_bid = json_real_value(json_array_get(json_array_get(bids, 0), 0));
    //}else{
    adj_price->lowest_ask = json_real_value(json_array_get(json_array_get(asks, 0), 0));
    //adj_price->lowest_ask += 300;
    
    //}
    
    // STORE BID AND ASK PRICE AT POS lock_index
    if (lock_index){
        if(mytype && (strcmp(mytype, "buy") == 0)){ //adj_price->lock_bid_ask contains bid/ask at position lock_index/
            
            int compressed_bids[lock_index];
            memset(&compressed_bids,0x0,sizeof(compressed_bids));
            for(int i = 0; i < json_array_size(bids); i++){
                lock_bid_pair = json_array_get(bids, i);
                lock_bid_price = (int)json_real_value(json_array_get(lock_bid_pair, 0));
                
                for(int i = 0; i < lock_index; i++){
                    if(compressed_bids[i] == 0){
                        if(compressed_bids[i-1] != lock_bid_price){
                            compressed_bids[i] = lock_bid_price;
                        }
                        break;
                    }
                    
                }
            }
            //adj_price->lock_bid_ask = (int)json_real_value(json_array_get(json_array_get(bids, lock_index), 0));
            adj_price->lock_bid_ask = compressed_bids[lock_index-1];
            /*for(int i = 0; i < lock_index; i++){
             printw("%d\n",compressed_bids[i]);
             }
             printw("CURRENT LOCK IS %d -> %f\n", lock_index, adj_price->lock_bid_ask);
             refresh();*/
            
            
            
            
        }else{
            int compressed_asks[lock_index];
            memset(&compressed_asks,0x0,sizeof(compressed_asks));
            for(int i = 0; i < json_array_size(asks); i++){
                lock_ask_pair = json_array_get(asks, i);
                lock_ask_price = (int)json_real_value(json_array_get(lock_ask_pair, 0));
                
                for(int i = 0; i < lock_index; i++){
                    if(compressed_asks[i] == 0){
                        if(compressed_asks[i-1] != lock_ask_price){
                            compressed_asks[i] = lock_ask_price;
                        }
                        break;
                    }
                    
                    
                    
                }
            }
            
            
            
            //adj_price->lock_bid_ask = (int)json_real_value(json_array_get(json_array_get(asks, lock_index), 0));
            adj_price->lock_bid_ask = compressed_asks[lock_index-1];
            
        }
    }
    
    
    int lockarrow = 0;
    
    for(int i = 0; i < 40; i++){
        //for(int i = 0; i < json_array_size(asks); i++){
        
        bid_pair = json_array_get(bids, i);
        bid_price = json_real_value(json_array_get(bid_pair, 0));
        bid_btc = json_real_value(json_array_get(bid_pair, 1));
        
        ask_pair = json_array_get(asks, i);
        ask_price = json_real_value(json_array_get(ask_pair, 0));
        ask_btc = json_real_value(json_array_get(ask_pair, 1));
        
        
        
        attron(COLOR_PAIR(3));
        
        printw("%3d. %9.6f ", i + 1, bid_btc);
        
        
        
        /////////////////////////////// HIGHLIGHT CURRENT BID/ASK POSITION /////////////////////////////
        
        if((mytype && strcmp(mytype, "buy") == 0 && myprice == bid_price)){
            //printw("PRICE HERE IS %f", myprice);
            if(!price_index){
                adj_price->higher_bid_ask = (int)json_real_value(json_array_get(json_array_get(bids, i-1), 0));    /* higher bid*/
                adj_price->lower_bid_ask = (int)json_real_value(json_array_get(json_array_get(bids, i+2), 0));    /* lower bid*/
            }else{
                adj_price->index_bid_ask = (int)json_real_value(json_array_get(json_array_get(bids, i+price_index), 0));
            }
            
            attron(COLOR_PAIR(3));
            printw("(%4.2f)", bid_price);
        }else{
            //printw("PRICE IS %f", myprice);
            //printw("\n\nBID_PRICE IS %f", bid_price);
            
            attron(COLOR_PAIR(2));
            printw("%4.2f ", bid_price);
        }
        
        
        if(((mytype && strcmp(mytype, "sell") == 0 && myprice == ask_price))){
            
            if(!price_index){
                adj_price->higher_bid_ask = (int)json_real_value(json_array_get(json_array_get(asks, i+2), 0)); /* higher ask */
                adj_price->lower_bid_ask = (int)json_real_value(json_array_get(json_array_get(asks, i-1), 0)); /* lower ask */
            }else{
                adj_price->index_bid_ask = (int)json_real_value(json_array_get(json_array_get(asks, i+price_index), 0));
                
            }
            attron(COLOR_PAIR(3));
            printw("(%4.2f) ", ask_price);
            
        }else{
            attron(COLOR_PAIR(1));
            printw("%4.2f ", ask_price);
        }
        
        
        
        attron(COLOR_PAIR(3));
        printw("%9.6f", ask_btc);
        
        
        if(lock_index && strcmp(mytype, "buy")==0){
            if(adj_price->lock_bid_ask == (int)bid_price)
                if(!lockarrow){
                    printw(" <-L%d", lock_index);
                    lockarrow = 1;
                }
        }
        
        if(lock_index && strcmp(mytype, "sell")==0){
            if(adj_price->lock_bid_ask == (int)ask_price){
                if(!lockarrow){
                    printw(" <-L%d", lock_index);
                    lockarrow=1;
                }
            }
        }
        ///////////////////////////////// PLACE LOCK INDEX MARKER ///////////////////////////////////////////
        
        //if((lock_index && lock_index == i+1)){
        // if((lock_index)){
        
        //}
        
        /*    printw(" <-L\n");
         }else{
         printw("\n");
         }*/
        ///////////////////////////////// END PLACE LOCK INDEX MARKER //////////////////////////////////////
        
        printw("\n");
        /////////////////////////////// END HIGHLIGHT CURRENT BID/ASK POSITION /////////////////////////////
        
        
        
        ////////////////////////////// STORE DEFAULT HIGHER BID/ASK FOR ORDERS OUTSIDE TOP LIST ///////////////////////////////
        if(adj_price->higher_bid_ask == 0 && i == json_array_size(asks)){
            if ((mytype && strcmp(mytype, "buy") == 0)){
                adj_price->higher_bid_ask = bid_price; //last bid_price value in the previous loop
            }else{
                adj_price->higher_bid_ask = ask_price; //last ask_price value in the previous loop
            }
        }
        ////////////////////////////// END STORE DEFAULT HIGHER BID/ASK FOR ORDERS OUTSIDE TOP LIST ///////////////////////////////
        
    }
    
}

/*---------------------------- end show_order_book ------------------------------*/




/*----------------------------------- main --------------------------------------*/

int main(void){
    
    
    json_t *orders, *orders_top, *ticker_root, *lastprice_root;
    json_error_t error;
    
    
    double cost = 0.0;
    double btc_available = 0.0;
    double usd_available = 0.0;
    float trade_price = 0.0;
    double high = 0.0;
    double low = 0.0;
    double lastprice = 0.0;
    
    char *order_type = NULL;
    
    const char *newtype = NULL;
    struct authdata *a = NULL;
    char ch;
    
    char *request_params = malloc(3000);
    char nonce[11];
    char timestamp[30];
    
    int price_index = 0;
    int lock_index = 5;
    long start_time = 0;
    int updatetrades_count = 0;
    int ticker_count = 0;
    int kickcount = 0;
    int lastprice_count = 0;
    char oldtype[6];
    
    start_time = time(NULL)+300;
    
    //////////////////////////////////////////////////////
    // PARSE CONFIG FILE
    
    
    
    
    
    
    
    
    // END PARSE CONFIG FILE
    /////////////////////////////////////////////////////
    
    
  
    
    
    
    
    
    
    
    
    
    
    ARCHIVE_DBS *archivedbs;
    archivedbs = malloc(sizeof(ARCHIVE_DBS));
    initialize_archivedbs(archivedbs);
    set_db_filenames(archivedbs);
    databases_setup(archivedbs, "ctrader", NULL);
    // UPDATE TRADE HISTORY DATABASE
    printf("Updating Trades database..\n");
    memset(nonce, 0, strlen(nonce));
    memset(request_params, 0, strlen(request_params));
    memset(timestamp, 0, strlen(timestamp));
    get_trades(archivedbs, nonce, request_params, timestamp, a, "update", "d",0); //DOWNLOAD ALL DONE TRADES
    memset(nonce, 0, strlen(nonce));
    memset(request_params, 0, strlen(request_params));
    memset(timestamp, 0, strlen(timestamp));
    get_trades(archivedbs, nonce, request_params, timestamp, a, "update", "cd",0); //DOWNLOAD ALL PARTIAL DONE TRADES
    
    /// GET LAST TRADE
    memset(nonce, 0, strlen(nonce));
    memset(request_params, 0, strlen(request_params));
    memset(timestamp, 0, strlen(timestamp));
    TRADE last_trade=get_trades(archivedbs, nonce, request_params, timestamp, a, "view", "d",1);
    databases_close(archivedbs);
    free(archivedbs);
    /////////////////
    
    
    initscr();
    cbreak();
    nodelay(stdscr, TRUE);
    noecho();
    scrollok(stdscr, TRUE);
    
    start_color();
    init_pair(1, COLOR_YELLOW, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_WHITE, COLOR_BLACK);
    struct prices *adj_price = (void*)malloc(sizeof(struct prices));
    memset(adj_price,0,sizeof(struct prices));
    
    
    
    
    
    //////////////////////// SET UP TALLY BOARD ASK / BID TARGET PRICE FOR AUTO TRADE MODE ////////////////////////////
    char *ticker_url = malloc(strlen("https://cex.io/api/ticker/BTC/USD/")+1);
    strcpy(ticker_url, "https://cex.io/api/ticker/BTC/USD/");
    struct RespData *response = (void*)malloc(sizeof(struct RespData));
    response->memory = (void*)malloc(1);
    response->size = 0;
    Getjson(response, ticker_url, NULL);
    free(ticker_url);
    ticker_root = json_loads(response->memory, 0, &error);
    free(response->memory);
    low = atof(json_string_value(json_object_get(ticker_root, "low")));
    high = atof(json_string_value(json_object_get(ticker_root, "high")));
    json_decref(ticker_root);
    
    
    float midpoint = ((high + low) / 2);
    int ask_tally_size = ((int)high - (int)midpoint) * 10;
    int bid_tally_size = ((int)midpoint - (int)low)  * 10;
    
    int tally_counter = 0;
    float target_price_sell = 0.0;
    float target_price_buy = 0.0;
    int target_price_tally = 0.0;
    
    
    /// ASK TALLY
    int **ask_tally = malloc(sizeof(int*) * ask_tally_size);
    for(int i = 0; i < ask_tally_size; i++){
        ask_tally[i] = malloc(sizeof(float));
        *ask_tally[i] = 0;
    }
    
    /// BID TALLY
    int **bid_tally = malloc(sizeof(int*) * bid_tally_size);
    for(int i = 0; i < bid_tally_size; i++){
        bid_tally[i] = malloc(sizeof(float));
        *bid_tally[i] = 0;
    }
    //////////////////////// END SET UP TALLY BOARD ASK / BID TARGET PRICE FOR AUTO TRADE MODE ////////////////////////
    
    
    
    
    
    for (;;)
    {
        ++updatetrades_count;
        if(updatetrades_count == 3000){
            //clear();
            printw("Updating Trades database..\n");
            refresh();
            //erase();
            updatetrades_count = 0;
            ARCHIVE_DBS *archivedbs = malloc(sizeof(ARCHIVE_DBS));
            initialize_archivedbs(archivedbs);
            set_db_filenames(archivedbs);
            databases_setup(archivedbs, "ctrader", NULL);
            // UPDATE TRADE HISTORY DATABASE
            memset(nonce, 0, strlen(nonce));
            memset(request_params, 0, strlen(request_params));
            memset(timestamp, 0, strlen(timestamp));
            
            get_trades(archivedbs, nonce, request_params, timestamp, a, "update", "d",0);
            databases_close(archivedbs);
            free(archivedbs);
        }
        
        
        
        
        struct order openorders;
        struct RespData *response = (void*)malloc(sizeof(struct RespData));
        
        char *replace_json = malloc(strlen("{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"type\":\"%s\",\"amount\":\"%f\",\"price\":\"%f\",\"order_id\":\"%s\"}")+1);
        strcpy(replace_json, "{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"type\":\"%s\",\"amount\":\"%f\",\"price\":\"%f\",\"order_id\":\"%s\"}");
        char *replace_url = malloc(strlen("https://cex.io/api/cancel_replace_order/BTC/USD/")+1);
        strcpy(replace_url,"https://cex.io/api/cancel_replace_order/BTC/USD/");
        
        
        char *cancel_json = malloc(strlen("{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"id\":\"%s\"}")+1);
        strcpy(cancel_json, "{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"id\":\"%s\"}");
        char *cancel_url = malloc(strlen("https://cex.io/api/cancel_order/")+1);
        strcpy(cancel_url, "https://cex.io/api/cancel_order/");
        
        char *open_order_json = malloc(strlen("{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\"}")+1);
        strcpy(open_order_json, "{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\"}");
        char *open_order_url = malloc(strlen("https://cex.io/api/open_orders/")+1);
        strcpy(open_order_url, "https://cex.io/api/open_orders/");
        
        char *order_book_url = malloc(strlen("https://cex.io/api/order_book/BTC/USD/")+1);
        strcpy(order_book_url, "https://cex.io/api/order_book/BTC/USD/");
        char *order_book_top_url = malloc(strlen("https://cex.io/api/order_book/BTC/USD/?depth=1")+1);
        strcpy(order_book_top_url, "https://cex.io/api/order_book/BTC/USD/?depth=1");
        
        char *ticker_url = malloc(strlen("https://cex.io/api/ticker/BTC/USD/")+1);
        strcpy(ticker_url, "https://cex.io/api/ticker/BTC/USD/");
        
        char *lastprice_url = malloc(strlen("https://cex.io/api/last_prices/BTC/USD/")+1);
        strcpy(lastprice_url, "https://cex.io/api/last_prices/BTC/USD/");
        
        char *balance_json = malloc(strlen("{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\"}")+1);
        strcpy(balance_json,"{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\"}" );
        char *balance_url = malloc(strlen("https://cex.io/api/balance/")+1);
        strcpy(balance_url, "https://cex.io/api/balance/");
        
        char *place_order_json = malloc(strlen("{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"type\":\"%s\",\"amount\":\"%f\",\"price\":\"%f\"}")+1);
        strcpy(place_order_json, "{\"key\":\"%s\",\"signature\":\"%s\",\"nonce\":\"%s\",\"type\":\"%s\",\"amount\":\"%f\",\"price\":\"%f\"}");
        char *place_order_url = malloc(strlen("https://cex.io/api/place_order/BTC/USD/")+1);
        strcpy(place_order_url, "https://cex.io/api/place_order/BTC/USD/");
        
        /////////////// GET TICKER /////////////////////////////////////////////////
        ticker_count++;
        if(ticker_count == 1 || ticker_count == 3){
            response->memory = (void*)malloc(1);
            response->size = 0;
            Getjson(response, ticker_url, NULL);
            ticker_root = json_loads(response->memory, 0, &error);
            free(response->memory);
            double oldlow = low;
            double oldhigh = high;
             low = atof(json_string_value(json_object_get(ticker_root, "low")));
            high = atof(json_string_value(json_object_get(ticker_root, "high")));
            
            if((oldlow != oldhigh) && (low < oldlow || high > oldhigh)){
                flash();
                beep();
            }
            json_decref(ticker_root);
            if (ticker_count == 3)
                ticker_count=0;
        }
        /////////////// END GET TICKER /////////////////////////////////////////////////
        
        /////////////// GET LAST PRICE /////////////////////////////////////////////////
        lastprice_count++;
        if(lastprice_count == 1 || lastprice_count == 3){
            response->memory = (void*)malloc(1);
            response->size = 0;
            Getjson(response, lastprice_url, NULL);
            lastprice_root = json_loads(response->memory, 0, &error);
            free(response->memory);
            json_t *last_price_data, *data_pair;
            last_price_data = json_object_get(lastprice_root, "data");
            //for(int i = 0; i < json_array_size(last_price_data); i++){
            data_pair = json_array_get(last_price_data, 0);
            lastprice = atof(json_string_value(json_object_get(data_pair, "lprice")));
            //}
            
            
            json_decref(lastprice_root);
            if (lastprice_count == 3)
                lastprice_count=0;
        }
        /////////////// END GET LAST PRICE /////////////////////////////////////////////////
        
        
        
        /////////////// GET OPEN ORDERS /////////////////////////////////////////////////
        memset(nonce, 0, strlen(nonce));
        memset(request_params, 0, strlen(request_params));
        strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
        a =  (void *)malloc(sizeof(struct authdata));
        create_authdata(a, nonce);
        sprintf(request_params, open_order_json, a->apikey, a->signature, nonce);
        free(a);
        response->memory = (void*)malloc(1);
        response->size = 0;
        Getjson(response, open_order_url, request_params);
        open_orders_root = json_loads(response->memory, 0, &error);
        free(response->memory);
        
        if (json_object_get(json_array_get(open_orders_root, 0), "type")){
            openorders.placed = 1;
            strcpy(openorders.type, json_string_value(json_object_get(json_array_get(open_orders_root, 0), "type")));
            strcpy(oldtype,openorders.type);
            openorders.amount = atof(json_string_value(json_object_get(json_array_get(open_orders_root, 0), "amount")));
            openorders.price = atof(json_string_value(json_object_get(json_array_get(open_orders_root, 0), "price")));
            strcpy(openorders.order_id, json_string_value(json_object_get(json_array_get(open_orders_root, 0), "id")));
        }else{
            
            /////////////// GET CURRENT BALANCE /////////////////////////////////////////////////
            memset(nonce, 0, strlen(nonce));
            memset(request_params, 0, strlen(request_params));
            strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
            a =  (void *)malloc(sizeof(struct authdata));
            
            create_authdata(a, nonce);
            sprintf(request_params, balance_json, a->apikey, a->signature, nonce);
            free(a);
            response->memory = (void*)malloc(1);
            response->size = 0;
            Getjson(response, balance_url, request_params);
            account_balance_root = json_loads(response->memory, 0, &error);
            free(response->memory);
            if(json_string_value(json_object_get(json_object_get(account_balance_root, "BTC"), "available"))){
                btc_available = atof(json_string_value(json_object_get(json_object_get(account_balance_root, "BTC"), "available")));
            }
            if(json_string_value(json_object_get(json_object_get(account_balance_root, "USD"), "available"))){
                usd_available = atof(json_string_value(json_object_get(json_object_get(account_balance_root, "USD"), "available")));
            }
            json_decref(account_balance_root);
            /////////////// END GET CURRENT BALANCE /////////////////////////////////////////////////
            
            if(openorders.placed){ /// SEND NOTIFICATION IF ORDER WAS FULFILLED.
                if((btc_available  < openorders.amount && strcmp(oldtype,"sell")==0) || (usd_available < openorders.amount * openorders.price && strcmp(oldtype,"buy")==0)){
                    printw("%s order completed!", oldtype);
                    refresh();
                    beep();
                    beep();
                    beep();
                    flash();
                    ARCHIVE_DBS *archivedbs;
                    archivedbs = malloc(sizeof(ARCHIVE_DBS));
                    initialize_archivedbs(archivedbs);
                    set_db_filenames(archivedbs);
                    databases_setup(archivedbs, "ctrader", NULL);
                    
                    // UPDATE TRADE HISTORY DATABASE
                    printf("Updating Trades database..\n");
                    refresh();
                    memset(nonce, 0, strlen(nonce));
                    memset(request_params, 0, strlen(request_params));
                    memset(timestamp, 0, strlen(timestamp));
                    get_trades(archivedbs, nonce, request_params, timestamp, a, "update", "d",0); //DOWNLOAD ALL DONE TRADES
                    memset(nonce, 0, strlen(nonce));
                    memset(request_params, 0, strlen(request_params));
                    memset(timestamp, 0, strlen(timestamp));
                    get_trades(archivedbs, nonce, request_params, timestamp, a, "update", "cd",0); //DOWNLOAD ALL PARTIAL DONE TRADES
                    databases_close(archivedbs);
                    free(archivedbs);
                    /////////////////
                }else{
                    printw("%s order cancelled.", oldtype);
                }
                
                
                openorders.placed = 0;
            }else{
                ///////////////////////////// AUTO-PLACE ORDER /////////////////////////////////////////
                if(btc_available > .01 && usd_available <= 100){
                    //printw("ORDER TYPE IS SELL\n");
                    //refresh();
                    order_type = "sell";
                    if(adj_price->lowest_ask && ((high - adj_price->lowest_ask) < (adj_price->lowest_ask - low))){ //SELL AT PAST MID
                        if(target_price_sell){
                            printw("WE ARE PLACING ORDER AT %.02f\n", target_price_sell);
                            refresh();
                            //exit(0);
                            /*memset(nonce, 0, strlen(nonce));
                             memset(request_params, 0, strlen(request_params));
                             strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                             a =  (void *)malloc(sizeof(struct authdata));
                             create_authdata(a, nonce);
                             sprintf(request_params, place_order_json, a->apikey, a->signature, nonce, order_type, btc_available, target_price_sell);
                             
                             free(a);
                             response->memory = (void*)malloc(1);
                             response->size = 0;
                             Getjson(response, place_order_url, request_params);
                             free(response->memory);*/
                            
                        }else{
                            // index = (p - m) * 10
                            int i =  (adj_price->lowest_ask - (int)midpoint) * 10;
                            //printw("SAVING %f AT POSITION %d\n", adj_price->lowest_ask, i);
                            *ask_tally[i] = *ask_tally[i]+1;
                            tally_counter++;
                            //printw("TALLY COUNTER IS %d\n", tally_counter);
                            refresh();
                            if (tally_counter == 60){
                                for(int i = 0; i < ask_tally_size; i++){
                                    if(*ask_tally[i]){ //SKIP ELEMENTS == 0
                                        float order_price = ((float)i / 10) + (int)midpoint;
                                        if(*ask_tally[i] >= target_price_tally){
                                            target_price_tally = *ask_tally[i];
                                            target_price_sell = order_price;
                                        }
                                        printw("%.02f - %d\n",order_price,*ask_tally[i]);
                                        refresh();
                                    }
                                }
                                //tally_counter = 0;
                            }
                        }
                    }
                }else if(btc_available < .01 && usd_available >= 100){
                    order_type = "buy";
                    //printw("ORDER TYPE IS BUY\n");
                    refresh();
                    if(adj_price->highest_bid && ((adj_price->highest_bid - low) < (high - adj_price->highest_bid))){  //BUY BELOW MID
                        if(target_price_buy){
                            /*printw("WE ARE PLACING ORDER AT %.02f\n", target_price_buy);*/
                            refresh();
                            /*memset(nonce, 0, strlen(nonce));
                             memset(request_params, 0, strlen(request_params));
                             strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                             a =  (void *)malloc(sizeof(struct authdata));
                             create_authdata(a, nonce);
                             sprintf(request_params, place_order_json, a->apikey, a->signature, nonce, order_type, btc_available, target_price_sell);
                             
                             free(a);
                             response->memory = (void*)malloc(1);
                             response->size = 0;
                             Getjson(response, place_order_url, request_params);
                             free(response->memory);*/
                            
                        }else{
                            // index = (p - m) * 10
                            int i =  (((int)midpoint) - adj_price->highest_bid) * 10;
                            //printw("SAVING %f AT POSITION %d\n", adj_price->highest_bid, i);
                            *bid_tally[i] = *bid_tally[i]+1;
                            tally_counter++;
                            //printw("TALLY COUNTER IS %d\n", tally_counter);
                            refresh();
                            if (tally_counter == 8){
                                for(int i = 0; i < bid_tally_size; i++){
                                    if(*bid_tally[i]){ //SKIP ELEMENTS == 0
                                        float order_price = (int)midpoint - ((float)i / 10);
                                        if(*bid_tally[i] >= target_price_tally){
                                            target_price_tally = *bid_tally[i];
                                            target_price_buy = order_price;
                                        }
                                        printw("%.02f - %d\n",order_price,*bid_tally[i]);
                                        refresh();
                                    }
                                }
                                //tally_counter = 0;
                            }
                            
                        }
                    }
                    
                }
                
                ///////////////////////////// END AUTO-PLACE ORDER /////////////////////////////////////
                
            }
            memset(openorders.type, 0, strlen(openorders.type));
        }
        json_decref(open_orders_root);
        
        /////////////// END GET OPEN ORDERS /////////////////////////////////////////////////
        
        
        
        if (kbhit()) {
            //if(type != NULL){
            memset(nonce, 0, strlen(nonce));
            memset(request_params, 0, strlen(request_params));
            strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
            a =  (void *)malloc(sizeof(struct authdata));
            create_authdata(a,nonce);
            ch = getch();
            if (ch =='\033'){
                // if the first value is esc
                getch(); // skip the [
                switch(getch()) { // the real value
                    case 'A': //up arrow
                        if(strcmp(openorders.type, "sell") == 0){   /* up/sell */
                            for (int i = 0; openorders.price >= adj_price->lower_bid_ask; i++) //while price is higher than next lower ask
                                openorders.price = (int)adj_price->lower_bid_ask - i; //subtract 1 from next lower ask to be ahead of that position.
                        }else if(strcmp(openorders.type, "buy") == 0){   /* up/buy */
                            cost = openorders.price * openorders.amount; //current bid/ask price * amount
                            for (int i = 0; openorders.price <= adj_price->higher_bid_ask; i++) //while price is lower than next higher bid
                                openorders.price = (int)adj_price->higher_bid_ask + i; //add 1 from next higher bid to be ahead of that position.
                            if ((openorders.price * openorders.amount) > cost){
                                printw("New amount: %f @ %.0f. [Y]/Esc", cost / openorders.price, openorders.price);
                                refresh();
                                char confirm;
                                scanf("%c", &confirm);
                                if(confirm != '\033'){
                                    openorders.amount = cost / openorders.price;
                                }
                            }
                        }else{
                            lock_index-=2;
                            break;
                        }
                        
                        
                        sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, openorders.amount, openorders.price, openorders.order_id);
                        free(a);
                        response->memory = (void*)malloc(1);
                        response->size = 0;
                        Getjson(response, replace_url, request_params);
                        free(response->memory);
                        break;
                        
                    case 'B': //down arrow
                        
                        cost = (openorders.price * openorders.amount) + ((openorders.price * openorders.amount) * .0026); //current bid/ask price * amount
                        if(strcmp(openorders.type, "buy") == 0){ /* down/buy */
                            for (int i = 0; openorders.price >= adj_price->lower_bid_ask; i++){ //while price is higher than next lower bid
                                openorders.price = (int)adj_price->lower_bid_ask - i; //subtract 1 from next lower bid to be below that position
                            }
                            openorders.amount = (cost - (cost * .0026)) / openorders.price; //new btc amount based on lower price (ask for confirmation if short selling)
                            
                        }else if(strcmp(openorders.type, "sell") == 0){ /* down/sell */
                            for (int i = 0; openorders.price <= adj_price->higher_bid_ask; i++){ //while price is lower than next higher ask
                                openorders.price = (int)adj_price->higher_bid_ask + i; //add 1 to next higher ask to be below that position
                            }
                        }else{
                            lock_index+=2;
                            break;
                        }
                        
                        sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, openorders.amount, openorders.price, openorders.order_id);
                        free(a);
                        response->memory = (void*)malloc(1);
                        response->size = 0;
                        Getjson(response, replace_url, request_params);
                        free(response->memory);
                        break;
                        
                    default: //Esc only (cancels order)
                        
                        if (lock_index){
                            //printw("REMOVING LOCK INDEX\n");
                            lock_index = 0;
                            break;
                        }else{
                            sprintf(request_params, cancel_json, a->apikey, a->signature, nonce, openorders.order_id);
                            
                            free(a);
                            response->memory = (void*)malloc(1);
                            response->size = 0;
                            Getjson(response,cancel_url, request_params);
                            free(response->memory);
                            memset(nonce, 0, strlen(nonce));
                            lock_index = 0;
                            break;
                        }
                }
            }else if (ch == 'j' || ch == 'k' || ch == 'l'){ //if j | k | l followed by digit(s).
                int pos = 0;
                nodelay(stdscr, FALSE);
                echo();
                printw("\nIndex No.: ");
                scanw("%d", &pos);
                nodelay(stdscr, TRUE);
                noecho();
                
                if (ch == 'j'){
                    printw("skipping down %d position(s).", pos);
                    price_index = pos;
                }else if (ch == 'k'){
                    printw("skipping up %d position(s).", pos);
                    price_index = pos * -1;
                }else if (ch == 'l'){
                    lock_index = pos;
                    printw("setting price lock at position %d", pos);
                    
                }
                
            }else if (ch == 32){ //if space bar
                
                
                nodelay(stdscr, FALSE);
                echo();
                printw("\nNew Price: ");
                refresh();
                scanw("%f", &trade_price);
                nodelay(stdscr, TRUE);
                noecho();
                refresh();
                
                
                ////////////   RETRIEVE PRICE VALUE ENTERED, SET MAX @ < HIGHEST BID, MIN @ > LOWEST ASK  //////////////
                
                if (openorders.type[0] && (strcmp(openorders.type, "buy") == 0)){
                    
                    while((trade_price > adj_price->highest_bid)){
                        nodelay(stdscr, FALSE);
                        echo();
                        printw("\nmust be < highest bid (%f)\n", adj_price->highest_bid);
                        refresh();
                        scanw("%f", &trade_price);
                        nodelay(stdscr, TRUE);
                        noecho();
                        refresh();
                    }
                }else if (openorders.type[0] && (strcmp(openorders.type, "sell") == 0)){
                    
                    while((trade_price < adj_price->lowest_ask)){
                        nodelay(stdscr, FALSE);
                        echo();
                        printw("\nmust be > lowest ask (%f)\n", adj_price->lowest_ask);
                        refresh();
                        scanw("%f", &trade_price);
                        nodelay(stdscr, TRUE);
                        noecho();
                        refresh();
                    }
                }else{
                    
                    
                    /////////////// GET CURRENT BALANCE /////////////////////////////////////////////////
                    memset(nonce, 0, strlen(nonce));
                    memset(request_params, 0, strlen(request_params));
                    strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                    a =  (void *)malloc(sizeof(struct authdata));
                    
                    create_authdata(a, nonce);
                    sprintf(request_params, balance_json, a->apikey, a->signature, nonce);
                    free(a);
                    response->memory = (void*)malloc(1);
                    response->size = 0;
                    Getjson(response, balance_url, request_params);
                    account_balance_root = json_loads(response->memory, 0, &error);
                    free(response->memory);
                    btc_available = atof(json_string_value(json_object_get(json_object_get(account_balance_root, "BTC"), "available")));
                    usd_available = atof(json_string_value(json_object_get(json_object_get(account_balance_root, "USD"), "available")));
                    json_decref(account_balance_root);
                    
                    //////////////////////////////////////////////////////////////////////////////////////////////////////
                    
                    
                    
                    
                    /////////////// GET HIGHEST BID / LOWEST ASK BALANCE ADJUST ENTERED PRICE /////////////////////////////////////////////////
                    response->memory = (void*)malloc(1);
                    response->size = 0;
                    Getjson(response, order_book_top_url, NULL);
                    orders_top = json_loads(response->memory, 0, &error);
                    free(response->memory);
                    
                    if ((usd_available < 100.0) && (btc_available > .02)){
                        newtype = "sell";
                        openorders.amount = btc_available;
                        double top_ask_price = json_real_value(json_array_get(json_array_get(json_object_get(orders_top, "asks"), 0), 0));
                        while(trade_price < top_ask_price){
                            nodelay(stdscr, FALSE);
                            echo();
                            printw("\nmust be > highest ask (%f)\n", top_ask_price);
                            refresh();
                            scanw("%f", &trade_price);
                            nodelay(stdscr, TRUE);
                            noecho();
                            refresh();
                        }
                    }else{
                        newtype = "buy";
                        openorders.amount = (usd_available - (usd_available * .0026)) / trade_price;
                        double top_bid_price = json_real_value(json_array_get(json_array_get(json_object_get(orders_top, "bids"), 0), 0));
                        while(trade_price > top_bid_price){
                            nodelay(stdscr, FALSE);
                            echo();
                            printw("\nmust be < highest bid (%f)\n", top_bid_price);
                            refresh();
                            scanw("%f", &trade_price);
                            nodelay(stdscr, TRUE);
                            noecho();
                            refresh();
                        }
                        
                    }
                    json_decref(orders_top);
                    
                    
                    
                    
                    
                    
                    ////////////// PLACE BUY OR SELL ORDER //////////////////////////////////////////////
                    
                    memset(nonce, 0, strlen(nonce));
                    memset(request_params, 0, strlen(request_params));
                    strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                    a =  (void *)malloc(sizeof(struct authdata));
                    create_authdata(a, nonce);
                    sprintf(request_params, place_order_json, a->apikey, a->signature, nonce, newtype, (float)openorders.amount, trade_price);
                    
                    free(a);
                    response->memory = (void*)malloc(1);
                    response->size = 0;
                    Getjson(response, place_order_url, request_params);
                    free(response->memory);
                    /////////////////////////////////////////////////////////////////////////////////
                    
                    
                    
                }
                refresh();
                /////////////////////////////////////////////////////////////////////////////
                
                
                
                double newamount = cost / trade_price;
                
                if(openorders.type[0] && strcmp(openorders.type, "buy") == 0){
                    cost = openorders.price * openorders.amount; //current bid/ask price * amount
                    if ((trade_price * openorders.amount) > cost){
                        printw("New target amount: %f @ %.0f. [Y]/Esc", cost / trade_price, trade_price);
                        refresh();
                        char confirm;
                        scanf("%c", &confirm);
                        if(confirm != '\033'){
                            openorders.amount = cost / trade_price;
                            sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, openorders.amount, trade_price, openorders.order_id);
                            
                            free(a);
                            response->memory = (void*)malloc(1);
                            response->size = 0;
                            Getjson(response, replace_url, request_params);
                            free(response->memory);
                        }
                    }else{
                        sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, newamount, trade_price, openorders.order_id);
                        
                        free(a);
                        response->memory = (void*)malloc(1);
                        response->size = 0;
                        Getjson(response, replace_url, request_params);
                        free(response->memory);
                    }
                    
                }else if(openorders.type[0] && strcmp(openorders.type, "sell") == 0 ){
                    memset(nonce, 0, strlen(nonce));
                    memset(request_params, 0, strlen(request_params));
                    strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                    a =  (void *)malloc(sizeof(struct authdata));
                    
                    create_authdata(a, nonce);
                    printw("New %s order @ %.2f...\n", openorders.type, trade_price);
                    refresh();
                    sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, (float)openorders.amount, trade_price, openorders.order_id);
                    
                    free(a);
                    response->memory = (void*)malloc(1);
                    response->size = 0;
                    Getjson(response, replace_url, request_params);
                    free(response->memory);
                    
                }else{ //no type (no existing order)
                    ;//printw("PLACE SELL/BUY ORDER CODE HERE");;
                }
            }else if(ch == 104){
                
                const char *mode = "view";
                if(getch() == 104){
                    mode = "update";
                }
                
                
                
                
                ARCHIVE_DBS *archivedbs = malloc(sizeof(ARCHIVE_DBS));
                initialize_archivedbs(archivedbs);
                set_db_filenames(archivedbs);
                databases_setup(archivedbs, "ctrader", NULL);
                // VIEW TRADE HISTORY
                memset(nonce, 0, strlen(nonce));
                memset(request_params, 0, strlen(request_params));
                memset(timestamp, 0, strlen(timestamp));
                
                get_trades(archivedbs, nonce, request_params, timestamp, a, mode, "d",0);
                databases_close(archivedbs);
                free(archivedbs);
                char contchar;
                nodelay(stdscr, FALSE);
                echo();
                printw("\n<Enter to continue>");
                scanw("%c", &contchar);
                nodelay(stdscr, TRUE);
                noecho();
            }else{
                ;
                //printw("\nHit space to change/place order.\n");
            }
            refresh();
            
        } else { // if not kbhit(), just show order book
            
            
            
            
            ///////////////  SHOW ORDER BOOK ///////////////////////////////////////////////
            response->memory = (void*)malloc(1);
            response->size = 0;
            Getjson(response, order_book_url, NULL);
            orders = json_loads(response->memory, 0, &error);
            free(response->memory);
            show_order_book(orders, openorders.type, openorders.amount, openorders.price, adj_price, price_index, lock_index, low, high, lastprice, last_trade);
            json_decref(orders);
            ///////////////  END SHOW ORDER BOOK ///////////////////////////////////////////////
            
            
            
            if (price_index){ //replace order if there was index selected (0-9 then j or k);
                double newprice = 0.0;
                double newamount = 0.0;
                memset(nonce, 0, strlen(nonce));
                memset(request_params, 0, strlen(request_params));
                strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                a =  (void *)malloc(sizeof(struct authdata));
                create_authdata(a, nonce);
                
                cost = openorders.price * openorders.amount ; //current bid cost
                newprice = (int)adj_price->index_bid_ask; //adj_price->index_bid_ask contains the price @ selected price index.
                openorders.amount = cost / openorders.price;
                newamount = cost / newprice;
                
                if (((newprice * openorders.amount) > cost) && strcmp(openorders.type, "buy") == 0){
                    //if total cost of BTC at new target price is greater than what was spent on current bid.
                    //lower the amount of BTC to be purchased as per available funds (long position).
                    printw("New amount: %f @ %.0f. [Y]/Esc", newamount, newprice);
                    refresh();
                    char confirm;
                    scanf("%c", &confirm);
                    if(confirm != '\033'){
                        sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, newamount, newprice, openorders.order_id);
                        
                        free(a);
                        response->memory = (void*)malloc(1);
                        response->size = 0;
                        Getjson(response, replace_url, request_params);
                        free(response->memory);
                    }
                }else{
                    sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, openorders.amount, newprice, openorders.order_id);
                    
                    free(a);
                    response->memory = (void*)malloc(1);
                    response->size = 0;
                    Getjson(response, replace_url, request_params);
                    free(response->memory);
                }
            }
            
            if(lock_index){
                memset(nonce, 0, strlen(nonce));
                memset(request_params, 0, strlen(request_params));
                strcpy(nonce, itoa((unsigned long)time(NULL)+300, timestamp) );
                a =  (void *)malloc(sizeof(struct authdata));
                create_authdata(a, nonce);
                cost = openorders.price * openorders.amount ; //current bid cost
                
                
                if(openorders.type[0] && strcmp(openorders.type, "buy") == 0){
                    while(openorders.price >= adj_price->lock_bid_ask){
                        kickcount++;
                        beep();
                        flash();
                        if(kickcount==4 && lock_index <= 5){
                            lock_index++;
                            kickcount = 0;
                        }
                        openorders.price = adj_price->lock_bid_ask - 2.0;
                        openorders.amount = cost / openorders.price;
                        printw("adjusting buy price.. (%f @ %f)\n", openorders.price, openorders.amount);
                        refresh();
                        sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, (float)openorders.amount, openorders.price, openorders.order_id);
                        response->memory = (void*)malloc(1);
                        response->size = 0;
                        Getjson(response, replace_url, request_params);
                        free(response->memory);
                        
                        
                    }
                }else if(openorders.type[0] && strcmp(openorders.type, "sell") == 0){
                    while(openorders.price <= adj_price->lock_bid_ask){
                        beep();
                        flash();
                        kickcount++;
                        if(kickcount==4 && lock_index <= 5){
                            lock_index++;
                            kickcount = 0;
                        }
                        openorders.price = adj_price->lock_bid_ask + 2.0;
                        printw("adjusting sell price.. (%f @ %f)\n", openorders.price, openorders.amount);
                        refresh();
                        sprintf(request_params, replace_json, a->apikey, a->signature, nonce, openorders.type, openorders.amount, openorders.price, openorders.order_id);
                        response->memory = (void*)malloc(1);
                        Getjson(response, replace_url, request_params);
                        free(response->memory);
                    }
                }
                free(a);
            }
            
            price_index=0;
            memset(openorders.type, 0, strlen(openorders.type));
            memset(nonce, 0, strlen(nonce));
            refresh();
            
        }
        free(response);
        curl_global_cleanup();
        //free(adj_price);
        if(replace_json != NULL)
            free(replace_json);
        if(replace_url != NULL)
            free(replace_url);
        if(cancel_json != NULL)
            free(cancel_json);
        if(cancel_url != NULL)
            free(cancel_url);
        if(open_order_json != NULL)
            free(open_order_json);
        if(open_order_url != NULL)
            free(open_order_url);
        if(order_book_url != NULL)
            free(order_book_url);
        if(order_book_top_url != NULL)
            free(order_book_top_url);
        
        
    }
    
    return 0;
    
    
}

/*----------------------------------- main --------------------------------------*/



