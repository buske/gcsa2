/*
  Copyright (c) 2015, 2016 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef _GCSA_GCSA_H
#define _GCSA_GCSA_H

#include "files.h"

namespace gcsa
{

/*
  gcsa.h: The main public interface.
*/

//------------------------------------------------------------------------------

class GCSA
{
public:
  typedef gcsa::size_type  size_type;
  typedef sdsl::wt_huff<>  bwt_type;
  typedef sdsl::bit_vector bit_vector;

//------------------------------------------------------------------------------

  GCSA();
  GCSA(const GCSA& source);
  GCSA(GCSA&& source);
  ~GCSA();

  void swap(GCSA& another);
  GCSA& operator=(const GCSA& source);
  GCSA& operator=(GCSA&& source);

  size_type serialize(std::ostream& out, sdsl::structure_tree_node* v = nullptr, std::string name = "") const;
  void load(std::istream& in);

  const static std::string EXTENSION; // .gcsa

  const static size_type SHORT_RANGE = 5; // Different strategy for LF(range).
  const static size_type MAX_ERRORS = 100; // Suppress further error messages during verification.
  const static size_type RMQ_BUFFER = 4 * MEGABYTE; // How many ranges to buffer before computing the RMQs?

//------------------------------------------------------------------------------

  /*
    This is the main constructor. We build GCSA from the graph according to the parameters,
    using a given number of doubling steps. The construction is mostly disk-based. There
    are at most two graphs on disk at once, and the size of each graph is bounded by the
    size limit.
  */
  GCSA(InputGraph& graph, const ConstructionParameters& parameters = ConstructionParameters());

//------------------------------------------------------------------------------

  /*
    Algorithms using GCSA. See algorithms.h for more information.
  */

  bool verifyIndex(std::vector<KMer>& kmers, size_type kmer_length) const;
  bool verifyIndex(const InputGraph& graph) const;

  size_type countKMers(size_type k, bool force = false) const;

//------------------------------------------------------------------------------

  /*
    The high-level interface deals with path ranges and actual character values.
    locate() stores the node identifiers in the given vector in sorted order.
    If append is true, the results are appended to the existing vector.
    If sort is true, the results are sorted and the duplicates are removed.

    The implementation of find() is based on bidirectional iterators.

    If the pattern is longer than the order of the index, there may be false
    positives (but no false negatives). The results of such queries must be
    verified in the original graph.
  */

  template<class Iterator>
  range_type find(Iterator begin, Iterator end) const
  {
    if(begin == end) { return range_type(0, this->size() - 1); }

    --end;
    range_type range = this->charRange(this->alpha.char2comp[*end]);
    while(!Range::empty(range) && end != begin)
    {
      --end;
      range = this->LF(range, this->alpha.char2comp[*end]);
    }

    return range;
  }

  template<class Container>
  range_type find(const Container& pattern) const
  {
    return this->find(pattern.begin(), pattern.end());
  }

  template<class Element>
  range_type find(const Element* pattern, size_type length) const
  {
    return this->find(pattern, pattern + length);
  }

  inline size_type count(range_type range) const
  {
    if(Range::empty(range) || range.second >= this->size()) { return 0; }
    size_type res = this->extra_pointers.count(range.first, range.second) + Range::length(range);
    if(range.second > range.first) { res -= this->redundant_pointers.count(range.first, range.second - 1); }
    return res;
  }

  void locate(size_type path, std::vector<node_type>& results, bool append = false, bool sort = true) const;
  void locate(range_type range, std::vector<node_type>& results, bool append = false, bool sort = true) const;

//------------------------------------------------------------------------------

  /*
    The low-level interface deals with paths, path ranges, and contiguous character values.
    There are no sanity checks for the parameter values.
  */

  inline size_type size() const { return this->header.path_nodes; }
  inline size_type edgeCount() const { return this->header.edges; }
  inline size_type order() const { return this->header.order; }

  inline size_type sampleCount() const { return this->stored_samples.size(); }
  inline size_type sampleBits() const { return this->stored_samples.width(); }
  inline size_type sampledPositions() const { return this->sampled_path_rank(this->sampled_paths.size()); }

  inline range_type charRange(comp_type comp) const
  {
    return this->pathNodeRange(gcsa::charRange(this->alpha, comp));
  }

  inline range_type LF(range_type range, comp_type comp) const
  {
    range = this->bwtRange(range);
    range = gcsa::LF(this->bwt, this->alpha, range, comp);
    if(Range::empty(range)) { return range; }
    return this->pathNodeRange(range);
  }

  // Follow the first edge backwards.
  inline size_type LF(size_type path_node) const
  {
    path_node = this->startPos(path_node);
    auto temp = this->bwt.inverse_select(path_node);
    path_node = this->alpha.C[temp.second] + temp.first;
    return this->edge_rank(path_node);
  }

  // LF(range, c) for 1 <= c < sigma - 1.
  void LF(range_type range, std::vector<range_type>& results) const;

  inline bool sampled(size_type path_node) const { return this->sampled_paths[path_node]; }

  inline range_type sampleRange(size_type path_node) const
  {
    path_node = this->sampled_path_rank(path_node);
    range_type sample_range;
    sample_range.first = (path_node > 0 ? this->sample_select(path_node) + 1 : 0);
    sample_range.second = this->sample_select(path_node + 1);
    return sample_range;
  }

  inline node_type sample(size_type i) const { return this->stored_samples[i]; }

//------------------------------------------------------------------------------

  GCSAHeader                header;

  bwt_type                  bwt;
  Alphabet                  alpha;

  // The last BWT position in each path is marked with an 1-bit.
  bit_vector                path_nodes;
  bit_vector::rank_1_type   path_rank;
  bit_vector::select_1_type path_select;

  // The last outgoing edge from each path is marked with an 1-bit.
  bit_vector                edges;
  bit_vector::rank_1_type   edge_rank;
  bit_vector::select_1_type edge_select;

  // Paths containing samples are marked with an 1-bit.
  bit_vector                sampled_paths;
  bit_vector::rank_1_type   sampled_path_rank;

  // The last sample belonging to the same path is marked with an 1-bit.
  sdsl::int_vector<0>       stored_samples;
  bit_vector                samples;
  bit_vector::select_1_type sample_select;

  // Structures used for counting queries.
  SadaSparse                extra_pointers;
  SadaCount                 redundant_pointers;

//------------------------------------------------------------------------------

private:
  void copy(const GCSA& source);
  void setVectors();
  void initSupport();

  void locateInternal(size_type path, std::vector<node_type>& results) const;

//------------------------------------------------------------------------------

  inline size_type startPos(size_type path_node) const
  {
    return (path_node > 0 ? this->path_select(path_node) + 1 : 0);
  }

  inline size_type endPos(size_type path_node) const
  {
    return this->path_select(path_node + 1);
  }

  inline range_type bwtRange(range_type path_node_range) const
  {
    path_node_range.first = this->startPos(path_node_range.first);
    path_node_range.second = this->endPos(path_node_range.second);
    return path_node_range;
  }

  inline range_type pathNodeRange(range_type outgoing_range) const
  {
    outgoing_range.first = this->edge_rank(outgoing_range.first);
    outgoing_range.second = this->edge_rank(outgoing_range.second);
    return outgoing_range;
  }
};  // class GCSA

//------------------------------------------------------------------------------

} // namespace gcsa

#endif // _GCSA_GCSA_H
