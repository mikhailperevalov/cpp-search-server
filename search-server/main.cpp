#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    Document() = default;

    Document(int id, double relevance, int rating)
        : id(id)
        , relevance(relevance)
        , rating(rating) {
    }

    int id = 0;
    double relevance = 0.0;
    int rating = 0;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:

    SearchServer()
    {}

    explicit SearchServer(const string& stop_words) {
        SetStopWords(SplitIntoWords(stop_words));
    }

    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words) {
        SetStopWords(stop_words);
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {

        if (document_id < 0) {
            throw invalid_argument("Отрицательный Id документа");
        }
        if (documents_.count(document_id) > 0) {
            throw invalid_argument("Документ с таким id уже есть в системе");
        }
        const vector<string> words = SplitIntoWordsNoStop(document);

        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
        document_ids_.push_back(document_id);
    }

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(const string& raw_query, DocumentPredicate document_predicate) const {
        Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, document_predicate);

        sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
            if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
                return lhs.rating > rhs.rating;
            } else {
                return lhs.relevance > rhs.relevance;
            }
        });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(
            raw_query,
            [status](int document_id, DocumentStatus document_status, int rating) {
                return document_status == status;
            });
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    int GetDocumentId(int index) const {
        if (index >= 0 && index < GetDocumentCount()) {
            return document_ids_[index];
        }
        throw out_of_range("Индекс переданного документа выходит за пределы допустимого диапазона"s);
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        if (documents_.count(document_id) == 0) {
            throw invalid_argument("Документ не найден");
        }

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

        return tuple{matched_words, documents_.at(document_id).status};
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };
    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    vector<int> document_ids_;

    template <typename StringCollection>
    void SetStopWords(const StringCollection& stop_words) {
        for (const string& word : stop_words) {
            if (word != ""s) {
                if (!IsValidWord(word))
                    throw invalid_argument("Oбнаружены спец-символы ASCII 0-31"s);
                stop_words_.insert(word);
            }
        }
	}

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    static bool IsValidWord(const string& word) {
        // A valid word must not contain special characters
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }
	
    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsValidWord(word)) {
            throw invalid_argument("Некорректный ввод: "s + string(word));
            }
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
        if (text.empty()) {
        throw invalid_argument("Пустой запрос"s);
        }
        bool is_minus = false;
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw invalid_argument("Некорректный ввод: "s + string(text));
        }
        return {text,
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
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename DocumentPredicate>
    vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
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
            matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
        }
        return matched_documents;
    }
};

void PrintDocument(const Document& document) {
    cout << "{ "s
         << "document_id = "s << document.id << ", "s
         << "relevance = "s << document.relevance << ", "s
         << "rating = "s << document.rating << " }"s << endl;
}

void PrintMatchDocumentResult(int document_id, const vector<string>& words, DocumentStatus status) {
    cout << "{ "s
         << "document_id = "s << document_id << ", "s
         << "status = "s << static_cast<int>(status) << ", "s
         << "words ="s;
    for (const string& word : words) {
        cout << ' ' << word;
    }
    cout << "}"s << endl;
}

void AddDocument(SearchServer& search_server, int document_id, const string& document, DocumentStatus status,
                 const vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    } catch (const exception& e) {
        cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << endl;
    }
}

void FindTopDocuments(const SearchServer& search_server, const string& raw_query) {
    cout << "Результаты поиска по запросу: "s << raw_query << endl;
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    } catch (const exception& e) {
        cout << "Ошибка поиска: "s << e.what() << endl;
    }
}

void MatchDocuments(const SearchServer& search_server, const string& query) {
    try {
        cout << "Матчинг документов по запросу: "s << query << endl;
        const int document_count = search_server.GetDocumentCount();
        for (int index = 0; index < document_count; ++index) {
            const int document_id = search_server.GetDocumentId(index);
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    } catch (const exception& e) {
        cout << "Ошибка матчинга документов на запрос "s << query << ": "s << e.what() << endl;
    }
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

// ----------------------------------------------- Имплементация --------------------------------------------------
void AssertImpl( bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                 const string& hint )
{
    if (!value) {
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
void RunTestImpl( Function func, const string& funcName ) {
    func();
    cerr << funcName << " OK"s << endl;
}

// ----------------------------------------------------------------------------------------------------------------
#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))
#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))
#define RUN_TEST(func) RunTestImpl(func, #func)
// ------------------------------ Начало модульных тестов поисковой системы ---------------------------------------

void TestExcludeStopWord() {
    const string stop_words = "help to find my brown cat"s;
    SearchServer server(stop_words);
    server.AddDocument(0, stop_words, DocumentStatus::ACTUAL, {1, 2 , 3});

    ASSERT(server.FindTopDocuments(stop_words).empty());
}
//-----------------------------------------------------------------------------------------------------------------
void TestMatchingWithMinusWords() {
    const string query = "brown -cat"s;
    SearchServer server;
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
    const string stop_words = "dog";
    SearchServer server(stop_words);
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
void TestCorrectMinusWords(void) {
    {
        try {
            SearchServer server("incorrect minus -"s);
        }
        catch (const exception &) {
            ASSERT(true);
        }
    }

    {
        try {
            SearchServer server("incorrect - minus"s);
        }
        catch (const exception &) {
            ASSERT(true);
        }
    }

    {
        try {
            SearchServer server("- incorrect minus"s);
        }
        catch (const exception &) {
            ASSERT(true);
        }
    }

    {
        try {
            SearchServer server("incorrect --minus"s);
        }
        catch (const exception &) {
            ASSERT(true);
        }
    }

    {
        try {
            SearchServer server("correct-- minus"s);
        }
        catch (const exception &) {
            ASSERT(false);
        }
    }

    {
        try {
            SearchServer server("correct--minus"s);
        }
        catch (const exception &) {
            ASSERT(false);
        }
    }
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
    RUN_TEST(TestCorrectMinusWords);
}
// ----------------------------- Окончание модульных тестов поисковой системы -------------------------------------

int main() {
    try
    {
        TestSearchServer();
    }
    catch (const std::exception& e)
    {
        cout << e.what() << endl;
    }
    cout << "Search server testing finished"s << endl;
    SearchServer search_server("и в на"s);

    AddDocument(search_server, 1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {7, 2, 7});
    AddDocument(search_server, 1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, -1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, 3, "большой пёс скво\x12рец евгений"s, DocumentStatus::ACTUAL, {1, 3, 2});
    AddDocument(search_server, 4, "большой пёс скворец евгений"s, DocumentStatus::ACTUAL, {1, 1, 1});

    FindTopDocuments(search_server, "пушистый -пёс"s);
    FindTopDocuments(search_server, "пушистый --кот"s);
    FindTopDocuments(search_server, "пушистый -"s);

    MatchDocuments(search_server, "пушистый пёс"s);
    MatchDocuments(search_server, "модный -кот"s);
    MatchDocuments(search_server, "модный --пёс"s);
    MatchDocuments(search_server, "пушистый - хвост"s);
}
