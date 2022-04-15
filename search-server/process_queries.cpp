#include "process_queries.h"

std::vector<std::vector<Document>> ProcessQueries(const SearchServer& search_server,
                                                  const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> result(queries.size());
    
    transform(std::execution::par,
              queries.begin(), queries.end(),
              result.begin(),
              [&search_server] (const std::string& query) {
                  return search_server.FindTopDocuments(query);
              }
    );
    return result;
}

std::vector<Document> ProcessQueriesJoined(const SearchServer& search_server,
                                           const std::vector<std::string>& queries) {
    std::vector<Document> docs;
    
    for(const auto& loc_docs : ProcessQueries(search_server, queries)) {
        docs.insert(docs.end(), loc_docs.begin(), loc_docs.end());
    }
    return docs;    
}