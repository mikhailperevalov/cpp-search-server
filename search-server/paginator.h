#pragma once

#include <algorithm>
#include <iostream>
#include <iterator>
#include <vector>

template <typename Iterator>
class IteratorRange {
public:
    IteratorRange(Iterator begin, Iterator end);

    Iterator begin() const;

    Iterator end() const;

    size_t size() const;

private:
    Iterator first_, last_;
    size_t size_;
};

template <typename Iterator>
IteratorRange<Iterator>::IteratorRange(Iterator begin, Iterator end)
        : first_(begin)
        , last_(end)
        , size_(distance(first_, last_)) {}

template <typename Iterator>
Iterator IteratorRange<Iterator>::begin() const {
    return first_;
}

template <typename Iterator>
Iterator IteratorRange<Iterator>::end() const {
    return last_;
}

template <typename Iterator>
size_t IteratorRange<Iterator>::size() const {
    return size_;
}

template <typename Iterator>
std::ostream& operator<<(std::ostream& out, IteratorRange<Iterator> range) {
    for (Iterator it = range.begin(); it != range.end(); ++it) {
        out << "{ " << *it << " }";
    }
    return out;
    std::cout << std::endl;
}

template <typename Iterator>
class Paginator {
public:
    Paginator(Iterator begin, Iterator end, size_t page_size);

    auto begin() const;

    auto end() const;

    size_t size() const;

private:
    std::vector<IteratorRange<Iterator>> pages_;
};

template <typename Iterator>
Paginator<Iterator>::Paginator(Iterator begin, Iterator end, size_t page_size)
{
    for(size_t mov = distance(begin, end); mov > 0;) {
        size_t current_size = std::min(page_size, mov);
        Iterator current_it = next(begin, current_size);
        pages_.push_back({begin, current_it});

        begin = current_it;
        mov -= current_size;
    }
}
template <typename Iterator>
auto Paginator<Iterator>::begin() const {
    return pages_.begin();
}

template <typename Iterator>
auto Paginator<Iterator>::end() const {
    return pages_.end();
}

template <typename Iterator>
size_t Paginator<Iterator>::size() const {
    return pages_.size();
}

template <typename Container>
auto Paginate(const Container& c, size_t page_size) {
    return Paginator(begin(c), end(c), page_size);
}