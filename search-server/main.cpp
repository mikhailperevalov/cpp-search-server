#include <cassert>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <numeric>
#include <vector>
#include <algorithm>
#include <iostream>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            words.push_back(word);
            word = "";
        }

        else {
            word += c;
        }
    }
    words.push_back(word);
    return words;
}

struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document,
                     DocumentStatus status, const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id,
                           DocumentData{ ComputeAverageRating(ratings), status });
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    vector<Document> FindTopDocuments(const string& raw_query,
                                      DocumentStatus status) const {
        return FindTopDocuments(raw_query,
                                [status](int document_id,
                                         DocumentStatus doc_status,
                                         int rating) {
                                    return doc_status == status;
                                }
        );
    }

    template<typename Document_Predicate>
    vector<Document> FindTopDocuments(const string& raw_query,
                                      Document_Predicate document_predicate) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, document_predicate);
        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                 if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
                     return lhs.rating > rhs.rating;
                 }
                 else {
                     return lhs.relevance > rhs.relevance;
                 }
             });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;

    }

    int GetDocumentCount() const {

        return documents_.size();

    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query,
                                                        int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return { matched_words, documents_.at(document_id).status };
    }
private:

    struct DocumentData {
        int rating;
        DocumentStatus status;
    };
    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {
                text,
                is_minus,
                IsStopWord(text)
        };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(
                GetDocumentCount() * 1.0
                / word_to_document_freqs_.at(word).size());
    }

    template<typename Document_Predicate>
    vector<Document> FindAllDocuments(const Query& query,
                                      Document_Predicate document_predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(
                    word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status,
                                       document_data.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }
        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({ document_id, relevance,
                                          documents_.at(document_id).rating });
        }
        return matched_documents;
    }

};

void PrintDocument(const Document& document) {
    cout << "{ "s
         << "document_id = "s << document.id << ", "s
         << "relevance = "s << document.relevance << ", "s
         << "rating = "s << document.rating
         << " }"s << endl;
}

template <typename Element>
ostream& operator<<(ostream& out, const vector<Element>& container) {
    cout << "[";
    Print(out, container);
    cout << "]";
    return out;
}

template <typename Element>
ostream& operator<<(ostream& out, const set<Element>& container) {
    cout << "{";
    Print(out, container);
    cout << "}";
    return out;
}

template <typename Key, typename Value>
ostream& operator<<(ostream& out, const map<Key, Value>& container) {
    cout << "{";
    Print(out, container);
    cout << "}";
    return out;
}

template <typename Data>
ostream& Print(ostream& out, const Data container) {
    bool is_first = true;
    for (const auto& element : container) {
        if (!is_first) {
            out << ", "s;
        }
        is_first = false;
        out << element;
    }
    return out;
}

template <typename First, typename Second>
ostream& operator<<(ostream& out, const pair<First, Second>& p) {
    return out << p.first << ": "s << p.second;
}

void AssertImpl( bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                 const string& hint )
{
    if (!value)
    {
        cerr << file << "("s << line << "s): "s << func << ": "s;
        cerr << "ASSERT("s << expr_str << "s) failed."s;
        if ( !hint.empty() )
        {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}


template <typename Function>
void RunTestImpl( Function func, const string& funcName )
{
    func();
    cerr << funcName << " OK"s << endl;
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))
#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))
#define RUN_TEST(func) RunTestImpl(func, #func)

// ------------------------------ Начало модульных тестов поисковой системы ---------------------------------------
void TestExcludeStopWord() {
    const string stop_words = "help to find my brown cat"s;
    SearchServer server;
    server.SetStopWords(stop_words);
    server.AddDocument(0, stop_words, DocumentStatus::ACTUAL, {1, 2 , 3});

    ASSERT(server.FindTopDocuments(stop_words).empty());
}
//-----------------------------------------------------------------------------------------------------------------
void TestMatchingWithMinusWords() {
    const string query = "brown -cat"s;
    auto server = SearchServer{};
    server.AddDocument(1, string{"how to find my lost brown cat"}, DocumentStatus::ACTUAL, {1});
    const auto [text, status] = server.MatchDocument(query, 1);

    ASSERT(status == DocumentStatus::ACTUAL);
    ASSERT(text.empty());
}
//-----------------------------------------------------------------------------------------------------------------
void TestSortingByRelevance() {
    const auto query = "how to catch herring for my cat"s;
    SearchServer server;
    server.AddDocument(1, "my brown cat eats herring", DocumentStatus::ACTUAL, {5});
    server.AddDocument(2, "perfect fish for your cat", DocumentStatus::ACTUAL, {5});
    server.AddDocument(3, "where to buy herring", DocumentStatus::ACTUAL, {5, 2});
    server.AddDocument(4, "sea bass catch", DocumentStatus::ACTUAL, {5, 1, 2});
    server.AddDocument(5, "fishing like a pro", DocumentStatus::ACTUAL, {5, 1});
    const auto results = server.FindTopDocuments(query);

    ASSERT(is_sorted(results.cbegin(), results.cend(),
                     [](const auto& left, const auto& right){return left.relevance > right.relevance;}));
    ASSERT_EQUAL(results.size(), 4);
}
//-----------------------------------------------------------------------------------------------------------------
void TestRatingCalculation() {
    const string query = "cat"s;
    SearchServer server;
    server.AddDocument(1, string{"mr cat is so cute"}, DocumentStatus::ACTUAL, {1, 2, 3});
    ASSERT_EQUAL(server.FindTopDocuments(query)[0].rating, (1 + 2 + 3 ) / 3.0);
    ASSERT_EQUAL(server.FindTopDocuments(query).size(), 1);
}
//-----------------------------------------------------------------------------------------------------------------
void TestCorrectRelevanceCalculation() {
    const string query = "fat cat dog"s;
    SearchServer server;
    server.SetStopWords("dog");
    server.AddDocument(0, "fat cat", DocumentStatus::ACTUAL, {1});
    server.AddDocument(1, "cat", DocumentStatus::ACTUAL, {2});
    server.AddDocument(2, "nope", DocumentStatus::ACTUAL, {3});

    const vector<Document> results = server.FindTopDocuments(query);

    ASSERT(abs(server.FindTopDocuments(query)[0].relevance - 0.752038698388137083483684) < 1e-6);
    ASSERT(abs(server.FindTopDocuments(query)[1].relevance - 0.405465108108164384859151) < 1e-6);
    ASSERT_EQUAL(results.size(), 2);
}
//-----------------------------------------------------------------------------------------------------------------
void TestSearchWithPredicate() {
    const int id = 1;
    const string query = "document"s;
    const string text = "Add new document"s;

    SearchServer search_server;
    search_server.AddDocument(id, text, DocumentStatus::ACTUAL, {});

    auto test = [id](int document_id, const DocumentStatus& status, int rating) {
        return document_id != id;
    };

    ASSERT_EQUAL(search_server.FindTopDocuments(query)[0].id, id);
    ASSERT(search_server.FindTopDocuments(query, test).empty());
}
//-----------------------------------------------------------------------------------------------------------------
void TestAddedDocumentStatus() {

    const auto query = "cat"s;
    const int id = 1;
    const auto status = DocumentStatus::BANNED;
    SearchServer server;
    server.AddDocument(id, string{"brown cat"}, status, {1});
    ASSERT_EQUAL(server.FindTopDocuments(query, status)[0].id, id);
    ASSERT_EQUAL(server.FindTopDocuments(query, status).size(), 1);

    ASSERT(server.FindTopDocuments(query).empty());
}
//-----------------------------------------------------------------------------------------------------------------
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWord);
    RUN_TEST(TestMatchingWithMinusWords);
    RUN_TEST(TestSortingByRelevance);
    RUN_TEST(TestRatingCalculation);
    RUN_TEST(TestCorrectRelevanceCalculation);
    RUN_TEST(TestSearchWithPredicate);
    RUN_TEST(TestAddedDocumentStatus);
}
// ----------------------------- Окончание модульных тестов поисковой системы -------------------------------------

int main() {
    TestSearchServer();
    cout << "Search server testing finished"s << endl;

    SearchServer search_server;
    search_server.SetStopWords("how to or"s);
    search_server.AddDocument(0, "how to cook sea bass"s, DocumentStatus::ACTUAL, {1, -5, 2, 0});
    search_server.AddDocument(1, "how catch herring or sea bass"s, DocumentStatus::ACTUAL, {2, 3, 2, 60});
    search_server.AddDocument(2, "where to buy scotch"s, DocumentStatus::ACTUAL, {3, 11, -1, 2});

    for (const Document& document : search_server.FindTopDocuments("sea bass"s)) {
        PrintDocument(document);
    }
    return 0;
}
