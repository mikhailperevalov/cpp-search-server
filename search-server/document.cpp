#include "document.h"
using namespace std;

Document::Document(int id, double relevance, int rating)
	: id(id)
	, relevance(relevance)
	, rating(rating)
{}

Document::Document()
	: id(0)
	, relevance(0.0)
	, rating(0)
{} 

ostream& operator<<(ostream& out, const Document& document) {
    out 
        << "document_id = "s << document.id << ", "s
        << "relevance = "s << document.relevance << ", "s
        << "rating = "s << document.rating;
    return out;
}