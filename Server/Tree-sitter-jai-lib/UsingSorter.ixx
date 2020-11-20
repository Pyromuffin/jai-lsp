module;
#include <vector>
#include "TreeSitterJai.h"

export module UsingSorter;

/*
L <- Empty list that will contain the sorted nodes
while exists nodes without a permanent mark do
    select an unmarked node n
    visit(n)

function visit(node n)
    if n has a permanent mark then
        return
    if n has a temporary mark then
        stop   (not a DAG)

    mark n with a temporary mark

    for each node m with an edge from n to m do
        visit(m)

    remove temporary mark from n
    mark n with a permanent mark
    add n to head of L

    */


export void SortAndInjectUsings(std::vector<ScopeHandle> usings)
{
    std::vector<bool> temporary;
    temporary.resize(usings.size(), false);
    std::vector<bool> permanent;
    permanent.resize(usings.size(), false);

    std::vector<ScopeHandle> sorted;
    sorted.reserve(usings.size());


}