#pragma once

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <cmath>

using namespace std;

// TODO: Comment this


struct Edge
{
    string to;
    double exPrice;
    double fee;
    string exchange;
    string bidOrAsk;
};

class Graph
{
private:
    int m_graphEdges = 0;

public:
    Graph() {}
    unordered_map<string, vector<Edge> > adjacencyList;

    void addEdge(string from, string to, double fee, string exchange)
    {
        // TODO: Maybe consider fixing this log(1-fee) to just computing it once on feeMap setup
        adjacencyList[from].push_back({to, 0.0, log(1-fee), exchange, ""});
        adjacencyList[to].push_back({from, 0.0, log(1-fee), exchange, ""});
        m_graphEdges += 2;
    }

    void deleteEdge(string from, string to, string exchange)
    {
        vector<Edge> &edges = adjacencyList[from];
        for (int i = 0; i < edges.size(); i++)
        {
            if ((edges[i].to == to) && (edges[i].exchange == exchange))
            {
                edges.erase(edges.begin() + i);
                if (edges.size() == 0)
                    adjacencyList.erase(from);
                break;
            }
        }
        edges = adjacencyList[to];
        for (int i = 0; i < edges.size(); i++)
        {
            if ((edges[i].to == from) && (edges[i].exchange == exchange))
            {
                edges.erase(edges.begin() + i);
                if (edges.size() == 0)
                    adjacencyList.erase(to);
                break;
            }
        }
    }


    void updateEdge(string from, string to, double bidPrice, double askPrice, string exchange)
    {
        for (Edge& edge : adjacencyList[from])
        {
            if ((edge.to == to) && (edge.exchange == exchange))
            {
                edge.exPrice = log(bidPrice);
                edge.bidOrAsk = "bid";
                break;
            }
        }
        for (Edge& edge : adjacencyList[to])
        {
            if ((edge.to == from) && (edge.exchange == exchange))
            {
                edge.exPrice = log(1) - log(askPrice);
                edge.bidOrAsk = "ask";
                break;
            }
        }
    }

    int getVertexCount()
    {
        return adjacencyList.size();
    }

    int getEdgeCount()
    {
        return m_graphEdges;
    }

    void printGraph()
    {
        cout << "\nGraph Print in form of Adjaceny List: " << endl;
        for (auto it = adjacencyList.begin(); it != adjacencyList.end(); it++)
        {
            string vertex = it->first;
            vector<Edge> edges = it->second;
            cout << vertex << ": ";
            for (Edge edge : edges)
            {
                cout << "(" << edge.to << ", " << edge.exPrice << ", " << edge.fee << ", " << edge.exchange << ") ";
            }
            cout << endl;
        }
    }

    void printEdge(string from, string to, string exchange) {
        vector<Edge> edges = adjacencyList[from];
        
        // Loop through the edges to find the edge that ends at the specified vertex
        for (Edge edge : edges) {
            if (edge.to == to && exchange == edge.exchange) {
                // Print the edge information
                cout << "Edge from " << from << " to " << to << " with exchange " << edge.exchange << ": ";
                cout << "ExPrice = " << edge.exPrice << ", Fee = " << edge.fee << endl;
                return;
            }
        }
        
        // If the loop finishes without finding the edge, print an error message
        cout << "Error: no edge found from " << from << " to " << to << endl;
    }

    
};
