#include "document.h"

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

std::ostream& operator<<(std::ostream& out, const Document& document) {
    out 
        << "document_id = " << document.id << ", "
        << "relevance = " << document.relevance << ", "
        << "rating = " << document.rating;
    return out;
}