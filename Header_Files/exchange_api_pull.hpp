#pragma once

#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>

using namespace std;
using namespace std::chrono;



/*
*
* Determines size for buffer needed for data received from API responses
*
*/
static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

string toLowerCase(string str) {
    for (char& c : str) {
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }
    }
    return str;
}

/*
*
* Develops a HashMap of predetermined viable trading pairs to add to
* an active graph of trading pair exchanges
* Map Strucuture: Coin1Coin2 -> [Coin1, Coin2] 
*
*/
unordered_map<string, vector<string>> buildSymbolHashMap()
{
    // read from Symbol_Space_Split.txt and build a dictionary
    //TODO: replace with updated path after scraping script has been created 
    ifstream input_file("../../Symbol_Data_Files/Symbol_Space_Split.txt");
    string line, lineCopy;
    short int slashPos;
    unordered_map<string, vector<string>> symbolMap;


    if (input_file.is_open())
    {
        while (getline(input_file, line))
        {
            short strLen = line.length(), forSlashPos = line.find("/");
            string firstCoin = line.substr(1, forSlashPos-1);
            string secondCoin = line.substr(forSlashPos + 1, strLen - forSlashPos - 2);

            vector<string> symbolsVec = {firstCoin, secondCoin};
            string symbol_key = firstCoin + secondCoin;
            symbolMap[symbol_key] = symbolsVec;
        }
        input_file.close();
    }
    else
    {
        cout << "Unable to open symbol pruning file" << endl;
    }
    return symbolMap;
}


/*
*
* Develops a HashMap of exchanges and their spot trading fees
* Map Strucuture: Binance -> [0.002] 
* TODO: implement API request to get most up to date trading fees
* not public endpoint so authoritzation via API key is required
*
*/
unordered_map<string, double> buildFeeMap(){
    unordered_map<string, double> feeMap;
    feeMap["binance"] = 0.002;
    feeMap["bitget"] = 0.002;
    feeMap["bitmart"] = 0.005;
    feeMap["gateio"] = 0.003;
    feeMap["huboi"] = 0.002;
    feeMap["kucoin"] = 0.002;
    return feeMap;
}


/*
*
* Pull spot ticker data from Binance Exchange via API
* and add data to Graph
*
*/
void pullBinance(unordered_map<string, vector<string> > &symbolMap, Graph &g, mutex &symbolMapMutex, bool setGraph)
{
    CURL *curl;
    CURLcode res;
    string response;
    string exchange = "binance";
    double exchangeFee = 0.002;


    curl = curl_easy_init();
    if (curl)
    {
        // cout << "Start pull from " << exchange << "...";
        const char* exchangeURL = "https://api.binance.com/api/v3/ticker/bookTicker";
        curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
        {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        try
        {
            nlohmann::json json_data = nlohmann::json::parse(response);

            for (const auto &item : json_data)
            {
                string tradeSymbol = string(item["symbol"]);
                vector<string> assets = symbolMap[tradeSymbol];

                // Trading pair is not viable by user settings
                if (assets.size() != 2)
                    continue;

                string fromAsset = assets[0], toAsset = assets[1];
                string strBidPrice = item["bidPrice"], strAskPrice = item["askPrice"];
                double bidPrice = stod(strBidPrice), askPrice = stod(strAskPrice);
                if ((bidPrice == 0.0) || (askPrice == 0.0))
                {
                    continue;
                }
                if (setGraph)
                {
                    g.addEdge(fromAsset, toAsset, exchangeFee, exchange);
                }
                else
                {
                    g.updateEdge(fromAsset, toAsset, bidPrice, askPrice, exchange);
                }
            }
        }
        catch (const exception &e)
        {
            cout << "SSL EXCEPTION DETECTED" << endl;
        }
        
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull order book  data from Binance Exchange via API
* and return info
*
*/
void pullBinanceOrderBook(TrackProfit &spotTrade, vector<vector<double>> &orderBookData, int &nDepth)
{
    CURL *curl;
    CURLcode res;
    string response;
    vector<string> symbolCombo;
    symbolCombo.push_back(spotTrade.to + spotTrade.from); symbolCombo.push_back(spotTrade.from + spotTrade.to);
    string baseURL = "https://api.binance.com/api/v3/depth", query, URL;


    curl = curl_easy_init();
    if (curl)
    {
        for(const string &symbol : symbolCombo)
        {
            query = "?symbol=" + symbol + "&limit=" + to_string(nDepth);
            URL = baseURL + query;
            const char* exchangeURL = URL.c_str();
            curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            nlohmann::json json_data = nlohmann::json::parse(response);
            response.clear(); // Clear the response variable
            // invalid combination
            if (json_data.find("msg") != json_data.end()) 
            {
                continue;
            }
    
            // parse out each active order in orderbook
            for (int i = 0; i < nDepth; i++)
            {
                string strBidPrice = json_data["bids"][i][0], strBidAmt = json_data["bids"][i][1];
                double bidPrice = stod(strBidPrice), bidAmt = stod(strBidAmt);
                string strAskPrice = json_data["asks"][i][0], strAskAmt = json_data["asks"][i][1];
                double askPrice = stod(strAskPrice), askAmt = stod(strAskAmt);
                orderBookData[0][i] = bidPrice; orderBookData[1][i] = bidAmt;
                orderBookData[2][i] = askPrice; orderBookData[3][i] = askAmt;
            }
            // correct url has been traversed
            break;

        }
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull spot ticker data from bitget Exchange via API
* and add data to Graph
*
*/
void pullBitget(unordered_map<string, vector<string> > &symbolMap, Graph &g, mutex &symbolMapMutex, bool setGraph)
{
    CURL *curl;
    CURLcode res;
    string response;
    string exchange = "bitget";
    double exchangeFee = 0.002;

    curl = curl_easy_init();
    if (curl)
    {
        // cout << "Start pull from " << exchange << "...";
        const char* exchangeURL = "https://api.bitget.com/api/spot/v1/market/tickers";
        curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);


        // Check for errors
        if (res != CURLE_OK)
        {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        try {
            nlohmann::json json_data = nlohmann::json::parse(response);

            for (auto& item : json_data["data"]) {
                // symbol comes in uppercase with the coins seperated by a hyphen
                string tradeSymbol = item["symbol"];
                vector<string> assets = symbolMap[tradeSymbol];
                if (assets.size() != 2)
                {
                    continue;
                }
                string fromAsset = assets[0], toAsset = assets[1];
                string strBidPrice = item["buyOne"], strAskPrice = item["sellOne"];
                long double bidPrice = stod(strBidPrice), askPrice = stod(strAskPrice);
                if ((bidPrice == 0.0) || (askPrice == 0.0)){
                    continue;
                }
                if (setGraph){
                    g.addEdge(fromAsset, toAsset, exchangeFee, exchange);
                }
                else{
                    g.updateEdge(fromAsset, toAsset, bidPrice, askPrice, exchange);
                }
            }
        }
        catch (const exception &e)
        {
            cout << "SSL EXCEPTION DETECTED" << endl;
        }

        // cout << "Finished pull from " << exchange << "\n" << endl;
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull order book data from bitget Exchange via API
* and add data to Graph
*
*/
void pullBitgetOrderBook(TrackProfit &spotTrade, vector<vector<double>> &orderBookData, int &nDepth)
{
    CURL *curl;
    CURLcode res;
    string response;
    vector<string> symbolCombo;
    symbolCombo.push_back(spotTrade.to + spotTrade.from); symbolCombo.push_back(spotTrade.from + spotTrade.to);
    string baseURL = "https://capi.bitget.com/data/v1/market/depth", query, URL;

    curl = curl_easy_init();
    if (curl)
    {
        for(string &symbol : symbolCombo)
        {
            query = "?symbol=" + symbol;
            URL = baseURL + query;
            const char* exchangeURL = URL.c_str();
            curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            nlohmann::json json_data = nlohmann::json::parse(response);
            response.clear(); // Clear the response variable
            // invalid API request
            if (json_data["data"]["asks"].size() == 0) 
            {
                continue;
            }
    
            // parse out each active order in orderbook
            for (int i = 0; i < nDepth; i++)
            {
                string strBidPrice = json_data["data"]["bids"][i][0], strBidAmt = json_data["data"]["bids"][i][1];
                double bidPrice = stod(strBidPrice), bidAmt = stod(strBidAmt);
                string strAskPrice = json_data["data"]["asks"][i][0], strAskAmt = json_data["data"]["asks"][i][1];
                double askPrice = stod(strAskPrice), askAmt = stod(strAskAmt);
                orderBookData[0][i] = bidPrice; orderBookData[1][i] = bidAmt;
                orderBookData[2][i] = askPrice; orderBookData[3][i] = askAmt;
            }
            // correct url has been traversed
            break;

        }
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull spot ticker data from BitMart Exchange via API
*
*/
void pullBitMart(unordered_map<string, vector<string> > &symbolMap, Graph &g, mutex &symbolMapMutex, bool setGraph)
{
    CURL *curl;
    CURLcode res;
    string response;
    string exchange = "bitmart";
    double exchangeFee = 0.005;

    curl = curl_easy_init();
    if (curl)
    {
        // cout << "Start pull from " << exchange << "...";
        const char* exchangeURL = "https://api-cloud.bitmart.com/spot/v2/ticker";
        curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
        {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        try{
            nlohmann::json json_data = nlohmann::json::parse(response);
            
            for (auto& item : json_data["data"]["tickers"]) {
                // symbol comes in uppercase with the coins seperated by a hyphen
                string tradeSymbol = item["symbol"];
                int delimeterPos = tradeSymbol.find("_");
                tradeSymbol = tradeSymbol.substr(0, delimeterPos) + tradeSymbol.substr(delimeterPos+1);
                vector<string> assets = symbolMap[tradeSymbol];
                if (assets.size() != 2)
                {
                    continue;
                }
                string fromAsset = assets[0], toAsset = assets[1];
                string strBidPrice = item["best_bid"], strAskPrice = item["best_ask"];
                long double bidPrice = stod(strBidPrice), askPrice = stod(strAskPrice);
                if ((bidPrice == 0.0) || (askPrice == 0.0)){
                    continue;
                }
                if (setGraph){
                    g.addEdge(fromAsset, toAsset, exchangeFee, exchange);
                }
                else{
                    g.updateEdge(fromAsset, toAsset, bidPrice, askPrice, exchange);
                }

            }
           
        }
        catch (const exception &e)
        {
            cout << "SSL EXCEPTION DETECTED" << endl;
        }
        // cout << "Finished pull from " << exchange << "\n" << endl;
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull order book data from BitMart Exchange via API
*
*/
void pullBitMartOrderBook(TrackProfit &spotTrade, vector<vector<double>> &orderBookData, int &nDepth)
{
    CURL *curl;
    CURLcode res;
    string response;
    vector<string> symbolCombo;
    symbolCombo.push_back(spotTrade.to + "_" +spotTrade.from); symbolCombo.push_back(spotTrade.from + "_" + spotTrade.to);
    string baseURL = "https://api-cloud.bitmart.com/spot/v1/symbols/book", query, URL;

    curl = curl_easy_init();
    if (curl)
    {
        for(string &symbol : symbolCombo)
        {
            query = "?symbol=" + symbol + "&size=" + to_string(nDepth);
            URL = baseURL + query;
            const char* exchangeURL = URL.c_str();
            curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            nlohmann::json json_data = nlohmann::json::parse(response);
            response.clear(); // Clear the response variable
            // invalid API request
            if (json_data["message"] != "OK") 
            {
                continue;
            }
    
            // parse out each active order in orderbook
            for (int i = 0; i < nDepth; i++)
            {
                string strBidPrice = json_data["data"]["buys"][i]["price"], strBidAmt = json_data["data"]["buys"][i]["amount"];
                double bidPrice = stod(strBidPrice), bidAmt = stod(strBidAmt);
                string strAskPrice = json_data["data"]["sells"][i]["price"], strAskAmt = json_data["data"]["sells"][i]["amount"];
                double askPrice = stod(strAskPrice), askAmt = stod(strAskAmt);
                orderBookData[0][i] = bidPrice; orderBookData[1][i] = bidAmt;
                orderBookData[2][i] = askPrice; orderBookData[3][i] = askAmt;
            }
            // correct url has been traversed
            break;

        }
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull spot ticker data from Gate.io Exchange via API
* and add data to Graph
*
*/
void pullGateio(unordered_map<string, vector<string> > &symbolMap, Graph &g, mutex &symbolMapMutex, bool setGraph)
{
    CURL *curl;
    CURLcode res;
    string response;
    string exchange = "gateio";
    double exchangeFee = 0.003;

    curl = curl_easy_init();
    if (curl)
    {
        const char* exchangeURL = "https://api.gateio.ws/api/v4/spot/tickers";
        curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
        {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        try
        {
            nlohmann::json json_data = nlohmann::json::parse(response);
        
            for (auto& item : json_data) 
            {
                // symbol comes in uppercase with the coins seperated by a hyphen
                string tradeSymbol = item["currency_pair"];
                int delimeterPos = tradeSymbol.find("_");
                tradeSymbol = tradeSymbol.substr(0, delimeterPos) + tradeSymbol.substr(delimeterPos+1);
                vector<string> assets = symbolMap[tradeSymbol];
                if (assets.size() != 2)
                    continue;

                string fromAsset = assets[0], toAsset = assets[1];
                string strBidPrice = item["highest_bid"], strAskPrice = item["lowest_ask"];
                long double bidPrice = stod(strBidPrice), askPrice = stod(strAskPrice);
                if ((bidPrice == 0.0) || (askPrice == 0.0))
                {
                    continue;
                }
                if (setGraph)
                {
                    g.addEdge(fromAsset, toAsset, exchangeFee, exchange);
                }
                else
                {    
                    g.updateEdge(fromAsset, toAsset, bidPrice, askPrice, exchange);
                }
            }
        }
        catch (const exception &e)
        {
            cout << "SSL EXCEPTION DETECTED" << endl;
        }

        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull order book data from Gate.io Exchange via API
*
*/
void pullGateioOrderBook(TrackProfit &spotTrade, vector<vector<double>> &orderBookData, int &nDepth)
{
    CURL *curl;
    CURLcode res;
    string response;
    vector<string> symbolCombo;
    symbolCombo.push_back(spotTrade.to + "_" +spotTrade.from); symbolCombo.push_back(spotTrade.from + "_" + spotTrade.to);
    string baseURL = "https://api.gateio.ws/api/v4/spot/order_book", query, URL;

    curl = curl_easy_init();
    if (curl)
    {
        for(string &symbol : symbolCombo)
        {
            query = "?currency_pair=" + symbol + "&limit=" + to_string(nDepth);
            URL = baseURL + query;
            const char* exchangeURL = URL.c_str();
            curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            nlohmann::json json_data = nlohmann::json::parse(response);
            response.clear(); // Clear the response variable
            // invalid API request
            if (json_data.find("message") != json_data.end()) 
            {
                continue;
            }
    
            // parse out each active order in orderbook
            for (int i = 0; i < nDepth; i++)
            {
                string strBidPrice = json_data["bids"][i][0], strBidAmt = json_data["bids"][i][1];
                double bidPrice = stod(strBidPrice), bidAmt = stod(strBidAmt);
                string strAskPrice = json_data["asks"][i][0], strAskAmt = json_data["asks"][i][1];
                double askPrice = stod(strAskPrice), askAmt = stod(strAskAmt);
                orderBookData[0][i] = bidPrice; orderBookData[1][i] = bidAmt;
                orderBookData[2][i] = askPrice; orderBookData[3][i] = askAmt;
            }
            // correct url has been traversed
            break;

        }
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull spot ticker data from Huobi Exchange via API
* and add data to Graph
*
*/
void pullHuobi(unordered_map<string, vector<string> > &symbolMap, Graph &g, mutex &symbolMapMutex, bool setGraph)
{
    CURL *curl;
    CURLcode res;
    string response;
    string exchange = "huobi";
    double exchangeFee = 0.002;

    curl = curl_easy_init();
    if (curl)
    {
        // cout << "Start pull from " << exchange << "...";
        const char* exchangeURL = "https://api.huobi.pro/market/tickers";
        curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
        {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        try
        {
            nlohmann::json json_data = nlohmann::json::parse(response);
            for (auto& item : json_data["data"]) {
                // Huobi returns symbol name in all lower case where my hashmap is all uppercase
                string tradeSymbol = item["symbol"];
                string tradeSymbolUpper;
                for (char c : tradeSymbol){
                    tradeSymbolUpper += toupper(c);
                }
                tradeSymbolUpper = tradeSymbolUpper;
                vector<string> assets = symbolMap[tradeSymbolUpper];
                if (assets.size() != 2)
                    continue;

                string fromAsset = assets[0], toAsset = assets[1];
                long double bidPrice = item["bid"], askPrice = item["ask"];
                if ((bidPrice == 0.0) || (askPrice == 0.0)){
                    continue;
                }
                if (setGraph){
                    g.addEdge(fromAsset, toAsset, exchangeFee, exchange);
                }
                else{
                    g.updateEdge(fromAsset, toAsset, bidPrice, askPrice, exchange);
                }
                
            }
        }
        catch (const exception &e)
        {
            cout << "SSL EXCEPTION DETECTED" << endl;
        }

        // cout << "Finished pull from " << exchange << "\n" << endl;
        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull order book data from Huobi Exchange via API
*
*/
void pullHuobiOrderBook(TrackProfit &spotTrade, vector<vector<double>> &orderBookData, int &nDepth)
{
    CURL *curl;
    CURLcode res;
    string response;
    vector<string> symbolCombo;
    symbolCombo.push_back(toLowerCase(spotTrade.to + spotTrade.from)); 
    symbolCombo.push_back(toLowerCase(spotTrade.from + spotTrade.to));
    string baseURL = "https://api.huobi.pro/market/depth?type=step0", query, URL;

    curl = curl_easy_init();
    if (curl)
    {
        for(string &symbol : symbolCombo)
        {
            query = "&symbol=" + symbol + "&depth=" + to_string(nDepth);
            URL = baseURL + query;
            const char* exchangeURL = URL.c_str();
            curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            nlohmann::json json_data = nlohmann::json::parse(response);
            response.clear(); // Clear the response variable

            // invalid API request
            if (json_data.find("err-msg") != json_data.end()) 
            {
                continue;
            }
    
            // parse out each active order in orderbook
            for (int i = 0; i < nDepth; i++)
            {
                double bidPrice = json_data["tick"]["bids"][i][0], bidAmt = json_data["tick"]["bids"][i][1];
                double askPrice = json_data["tick"]["asks"][i][0], askAmt = json_data["tick"]["asks"][i][1];
                orderBookData[0][i] = bidPrice; orderBookData[1][i] = bidAmt;
                orderBookData[2][i] = askPrice; orderBookData[3][i] = askAmt;
            }
            // correct url has been traversed
            break;

        }
        curl_easy_cleanup(curl);
    }
}

/*
*
* Pull spot ticker data from Kucoin Exchange via API
* and add data to Graph
*
*/
void pullKucoin(unordered_map<string, vector<string> > &symbolMap, Graph &g, mutex &symbolMapMutex, bool setGraph)
{
    CURL *curl;
    CURLcode res;
    string response;
    string exchange = "kucoin";
    double exchangeFee;

    curl = curl_easy_init();
    if (curl)
    {
        const char* exchangeURL = "https://api.kucoin.com/api/v1/market/allTickers";
        curl_easy_setopt(curl, CURLOPT_URL, exchangeURL );
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
        {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        // TODO: Test to make sure something correct is returned from API call aka on the right VPN
        try
        {
            nlohmann::json json_data = nlohmann::json::parse(response);

            symbolMapMutex.lock();
            for (auto& item : json_data["data"]["ticker"]) {
                // symbol comes in uppercase with the coins seperated by a hyphen
                string tradeSymbol = item["symbol"];
                int delimeterPos = tradeSymbol.find("-");
                tradeSymbol = tradeSymbol.substr(0, delimeterPos) + tradeSymbol.substr(delimeterPos+1);
                vector<string> assets = symbolMap[tradeSymbol];
                if (assets.size() != 2)
                    continue;
                
                string fromAsset = assets[0], toAsset = assets[1];
                string strBidPrice = item["buy"], strAskPrice = item["sell"];
                long double bidPrice = stod(strBidPrice), askPrice = stod(strAskPrice);
                if ((bidPrice == 0.0) || (askPrice == 0.0)){
                    continue;
                }
                if (setGraph){
                    string strTakerFee = item["takerFeeRate"], strMakerFee = item["makerFeeRate"];
                    exchangeFee = stod(strTakerFee) + stod(strMakerFee);
                    g.addEdge(fromAsset, toAsset, exchangeFee, exchange);
                }
                else{
                    g.updateEdge(fromAsset, toAsset, bidPrice, askPrice, exchange);
                } 
            }
        }
        catch (const exception &e)
        {
            cout << "SSL EXCEPTION DETECTED" << endl;
        }

        curl_easy_cleanup(curl);
    }
}


/*
*
* Pull order book data from Kucoin Exchange via API
*
*/
void pullKucoinOrderBook(TrackProfit &spotTrade, vector<vector<double>> &orderBookData, int &nDepth)
{
    CURL *curl;
    CURLcode res;
    string response;
    vector<string> symbolCombo;
    symbolCombo.push_back(spotTrade.to + "-" + spotTrade.from); 
    symbolCombo.push_back(spotTrade.from + "-" + spotTrade.to);
    string baseURL = "https://api.kucoin.com/api/v1/market/orderbook/level2_100", query, URL;
    

    curl = curl_easy_init();
    if (curl)
    {
        for(string &symbol : symbolCombo)
        {
            query = "?symbol=" + symbol;
            URL = baseURL + query;
            const char* exchangeURL = URL.c_str();
            curl_easy_setopt(curl, CURLOPT_URL, exchangeURL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);

            // Check for errors
            if (res != CURLE_OK)
            {
                cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            nlohmann::json json_data = nlohmann::json::parse(response);
            response.clear(); // Clear the response variable

            // invalid API request
            if (json_data["data"]["time"] == 0) 
            {
                continue;
            }

            // parse out each active order in orderbook
            for (int i = 0; i < nDepth; i++)
            {
                string strBidPrice = json_data["data"]["bids"][i][0], strBidAmt = json_data["data"]["bids"][i][1];
                double bidPrice = stod(strBidPrice), bidAmt = stod(strBidAmt);
                string strAskPrice = json_data["data"]["asks"][i][0], strAskAmt = json_data["data"]["asks"][i][1];
                double askPrice = stod(strAskPrice), askAmt = stod(strAskAmt);
                orderBookData[0][i] = bidPrice; orderBookData[1][i] = bidAmt;
                orderBookData[2][i] = askPrice; orderBookData[3][i] = askAmt;
            }
            // correct url has been traversed
            break;

        }
        curl_easy_cleanup(curl);
    }
}

/*
*
* Parallel pull spot ticker data for all coins on all exchanges currently
* supported in this framework via API
*
*/
void pullAllTicker(unordered_map<string, vector<string> > &symbolMap, Graph &g, bool setGraph) {
    mutex symbolMapMutex;
    vector<thread> threads;
    threads.push_back(thread(pullBinance, ref(symbolMap), ref(g), ref(symbolMapMutex), ref(setGraph)));
    threads.push_back(thread(pullBitget, ref(symbolMap), ref(g), ref(symbolMapMutex), ref(setGraph)));
    threads.push_back(thread(pullBitMart, ref(symbolMap), ref(g), ref(symbolMapMutex), ref(setGraph)));
    threads.push_back(thread(pullGateio, ref(symbolMap), ref(g), ref(symbolMapMutex), ref(setGraph)));
    threads.push_back(thread(pullKucoin, ref(symbolMap), ref(g), ref(symbolMapMutex), ref(setGraph)));
    threads.push_back(thread(pullHuobi, ref(symbolMap), ref(g), ref(symbolMapMutex), ref(setGraph)));
    for (auto &thread : threads) {
        thread.join();
    }
}


/*
*
* Parallel pull spot ticker data for all coins on all exchanges currently
* supported in this framework via API
*
*/
void pullAllOrderBook(vector<TrackProfit> &arbPath, vector<vector<vector<double>>> &orderBookData, int &nDepth) {
    
    vector<thread> threads;
    for (int i = 0; i < arbPath.size(); i++)
    {
        if (arbPath[i].exchange == "binance")
            threads.push_back(thread(pullBinanceOrderBook, ref(arbPath[i]), ref(orderBookData[i]), ref(nDepth)));
        else if (arbPath[i].exchange == "bitget")
            threads.push_back(thread(pullBitgetOrderBook, ref(arbPath[i]), ref(orderBookData[i]), ref(nDepth)));
        else if (arbPath[i].exchange == "bitmart")
            threads.push_back(thread(pullBitMartOrderBook, ref(arbPath[i]), ref(orderBookData[i]), ref(nDepth)));
        else if (arbPath[i].exchange == "gateio")
            threads.push_back(thread(pullGateioOrderBook, ref(arbPath[i]), ref(orderBookData[i]), ref(nDepth)));
        else if (arbPath[i].exchange == "kucoin")
            threads.push_back(thread(pullKucoinOrderBook, ref(arbPath[i]), ref(orderBookData[i]), ref(nDepth)));
        else if (arbPath[i].exchange == "huobi")
            threads.push_back(thread(pullHuobiOrderBook, ref(arbPath[i]), ref(orderBookData[i]), ref(nDepth)));
    }
    for (auto &thread : threads) {
        thread.join();
    }
}


