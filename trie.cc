// Copyright Jianing Yang <jianingy.yang@gmail.com> 2009

#include <iostream>
#include <cassert>
#include "trie.h"

#define sanity_delete(X)  do { \
    if (X) { \
        delete (X); \
        (X) = NULL; \
    }} while (0);
#define sanity_delete_array(X)  do { \
    if (X) { \
        delete [](X); \
        (X) = NULL; \
    }} while (0);


const char double_trie::magic_[16] = "TWO_TRIE";
const char suffix_trie::magic_[16] = "TAIL_TRIE";

// ************************************************************************
// * Implementation of helper functions                                   *
// ************************************************************************

static const char* pretty_size(size_t size, char *buf, size_t buflen)
{
    assert(buf);
    if (size > 1024 * 1024 * 1024) {
        snprintf(buf, buflen, "%4.2fG", 
                 static_cast<double>(size) / (1024 * 1024 * 1024));
    } else if (size > 1024 * 1024) {
        snprintf(buf, buflen, "%4.2fM",
                 static_cast<double>(size) / (1024 * 1024));
    } else if (size > 1024) {
        snprintf(buf, buflen, "%4.2fK", static_cast<double>(size) / 1024);
    } else {
        snprintf(buf, buflen, "%4.2f", static_cast<double>(size));
    }
    return buf;
}

trie_interface::~trie_interface()
{
}

// ************************************************************************
// * Implementation of basic_trie                                         *
// ************************************************************************

basic_trie::basic_trie(size_type size,
                       trie_relocator_interface<size_type> *relocator)
    :header_(NULL), states_(NULL), last_base_(0), owner_(true),
     relocator_(relocator)
{
    if (size < kCharsetSize)
        size = kCharsetSize;
    header_ = new header_type();
    memset(header_, 0, sizeof(header_type));
    resize_state(size);
}

basic_trie::basic_trie(void *header, void *states)
    :header_(NULL), states_(NULL), last_base_(0), owner_(false),
     relocator_(NULL)
{
    header_ = static_cast<header_type *>(header);
    states_ = static_cast<state_type *>(states);
}

basic_trie::basic_trie(const basic_trie &trie)
    :header_(NULL), states_(NULL), last_base_(0), owner_(false),
     relocator_(NULL)
{
    clone(trie);
}

basic_trie &basic_trie::operator=(const basic_trie &trie)
{
    clone(trie);
    return *this;
}

void basic_trie::clone(const basic_trie &trie)
{
    if (owner_) {
        if (header_) {
            delete header_;
            header_ = NULL;
        }
        if (states_) {
            delete []states_;
            states_ = NULL;  // set to NULL for next resize
        }
    }
    owner_ = true;
    header_ = new header_type();
    states_ = new state_type[trie.header()->size];
    memcpy(header_, trie.header(), sizeof(header_type));
    memcpy(states_, trie.states(), trie.header()->size * sizeof(state_type));
}

basic_trie::~basic_trie()
{
    if (owner_) {
        sanity_delete(header_);
        sanity_delete_array(states_);
    }
}

basic_trie::size_type
basic_trie::find_base(const char_type *inputs, const extremum_type &extremum)
{
    bool found;
    size_type i;
    const char_type *p;

    for (i = last_base_, found = false; !found; /* empty */) {
        i++;
        if (i + extremum.max >= header_->size)
            resize_state(extremum.max);
        if (check(i + extremum.min) <= 0 && check(i + extremum.max) <= 0) {
            for (p = inputs, found = true; *p; p++) {
                if (check(i + *p) > 0) {
                    found = false;
                    break;
                }
            }
        } else {
            //  i += extremum.min;
        }
    }

    last_base_ = i;

    return last_base_;
}

basic_trie::size_type
basic_trie::relocate(size_type stand,
                     size_type s,
                     const char_type *inputs,
                     const extremum_type &extremum)
{
    size_type obase, nbase, i;
    char_type targets[kCharsetSize + 1];

    obase = base(s);  // save old base value
    nbase = find_base(inputs, extremum);  // find a new base

    for (i = 0; inputs[i]; i++) {
        if (check(obase + inputs[i]) != s)  // find old links
            continue;
        set_base(nbase + inputs[i], base(obase + inputs[i]));
        set_check(nbase + inputs[i], check(obase + inputs[i]));
        find_exist_target(obase + inputs[i], targets, NULL);
        for (char_type *p = targets; *p; p++) {
            set_check(base(obase + inputs[i]) + *p, nbase + inputs[i]);
        }
        // if where we are standing is moving, we move with it
        if (stand == obase + inputs[i])
            stand = nbase + inputs[i];
        if (relocator_)
            relocator_->relocate(obase + inputs[i], nbase + inputs[i]);
        // free old places
        set_base(obase + inputs[i], 0);
        set_check(obase + inputs[i], 0);
        // create new links according old ones
    }
    // finally, set new base
    set_base(s, nbase);

    return stand;
}

basic_trie::size_type
basic_trie::create_transition(size_type s, char_type ch)
{
    char_type targets[kCharsetSize + 1];
    char_type parent_targets[kCharsetSize + 1];
    extremum_type extremum = {0, 0}, parent_extremum = {0, 0};

    size_type t = next(s, ch);
    if (t >= header_->size)
        resize_state(t - header_->size + 1);

    if (base(s) > 0 && check(t) <= 0) {
        // Do Nothing !!
    } else {
        size_type num_targets =
            find_exist_target(s, targets, &extremum);
        size_type num_parent_targets = check(t)?
            find_exist_target(check(t), parent_targets, &parent_extremum):0;
        if (num_parent_targets > 0 && num_targets + 1 > num_parent_targets) {
            s = relocate(s, check(t), parent_targets, parent_extremum);
        } else {
            targets[num_targets++] = ch;
            targets[num_targets] = 0;
            if (ch > extremum.max || !extremum.max)
                extremum.max = ch;
            if (ch < extremum.min || !extremum.min)
                extremum.min = ch;
            s = relocate(s, s, targets, extremum);
        }
        t = next(s, ch);
        if (t >= header_->size)
            resize_state(t - header_->size + 1);
    }
    set_check(t, s);

    return t;
}


void basic_trie::insert(const char *inputs, size_t length, value_type val)
{
    if (val < 1)
        throw std::runtime_error("basic_trie::insert: value must > 0");

    if (!inputs)
        throw std::runtime_error("basic_trie::insert: input pointer is null");

    size_type s;
    const char *p;

    s = go_forward(1, inputs, length, &p);

    for (/* empty */; p < inputs + length; p++) {
        char_type ch = char_in(*p);
        s = create_transition(s, ch);
    }

    // add a terminator
    s = create_transition(s, kTerminator);
    set_base(s, val);
}


bool basic_trie::search(const char *inputs,
                        size_t length,
                        value_type *value) const
{
    if (!inputs)
        throw std::runtime_error("basic_trie::search: input pointer is null");

    size_type s = go_forward(1, inputs, length, NULL);
    size_type t = next(s, kTerminator);

    if (!check_transition(s, t))
        return false;

    if (value)
        *value = base(t);
    return true;
}

#ifndef NDEBUG
void basic_trie::trace(size_type s) const
{
    size_type num_target;
    char_type targets[kCharsetSize + 1];
    static std::vector<size_type> trace_stack;

    trace_stack.push_back(s);
    if ((num_target = find_exist_target(s, targets, NULL))) {
        for (char_type *p = targets; *p; p++) {
            size_type t = next(s, *p);
            if (t < header_->size)
                trace(next(s, *p));
        }
    } else {
        size_type cbase = 0, obase = 0;
        std::cerr << "transition => ";
        std::vector<size_type>::const_iterator it;
        for (it = trace_stack.begin();it != trace_stack.end(); it++) {
            cbase = base(*it);
            if (obase) {
                if (*it - obase == kTerminator) {
                    std::cerr << "-#->";
                } else {
                    char ch = char_out(*it - obase);
                    if (isgraph(ch))
                        std::cerr << "-'" << ch << "'->";
                    else
                        std::cerr << "-<" << std::hex
                                  << static_cast<int>(ch) << ">->";
                }
            }
            std::clog << *it << "[" << cbase << "]";
            obase = cbase;
        }
        std::cerr << "->{" << std::dec << (cbase) << "}" << std::endl;
    }
    trace_stack.pop_back();
}
#endif

// ************************************************************************
// * Implementation of two trie                                           *
// ************************************************************************

double_trie::double_trie()
    :header_(NULL), lhs_(NULL), rhs_(NULL), index_(NULL), accept_(NULL),
     next_accept_(1), next_index_(1), front_relocator_(NULL),
     rear_relocator_(NULL), stand_(0), mmap_(NULL), mmap_size_(0)
{
    header_ = new header_type();
    memset(header_, 0, sizeof(header_type));
    snprintf(header_->magic, sizeof(header_->magic), "%s", magic_);
    front_relocator_ = new trie_relocator<double_trie>
                           (this, &double_trie::relocate_front);
    rear_relocator_ = new trie_relocator<double_trie>
                          (this, &double_trie::relocate_rear);
    lhs_ = new basic_trie();
    rhs_ = new basic_trie();
    lhs_->set_relocator(front_relocator_);
    rhs_->set_relocator(rear_relocator_);
    header_->index_size = 1024;
    index_ = resize<index_type>(NULL, 0, header_->index_size);
    header_->accept_size = 1024;
    accept_ = resize<accept_type>(NULL, 0, header_->accept_size);
}

double_trie::double_trie(const char *filename)
    :header_(NULL), lhs_(NULL), rhs_(NULL), index_(NULL), accept_(NULL),
     next_accept_(1), next_index_(1), front_relocator_(NULL),
     rear_relocator_(NULL), stand_(0), mmap_(NULL), mmap_size_(0)
{
    struct stat sb;
    int fd, retval;

    if (!filename)
        throw std::runtime_error(std::string("can not load from file ")
                                 + filename);

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        throw std::runtime_error(strerror(errno));
    if (fstat(fd, &sb) < 0)
        throw std::runtime_error(strerror(errno));

    mmap_ = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmap_ == MAP_FAILED)
        throw std::runtime_error(strerror(errno));
    while (retval = close(fd), retval == -1 && errno == EINTR) {
        // exmpty
    }
    mmap_size_ = sb.st_size;

    void *start;
    start = header_ = reinterpret_cast<header_type *>(mmap_);
    if (strcmp(header_->magic, magic_))
        throw std::runtime_error("file corrupted");
    // load index
    start = index_ = reinterpret_cast<index_type *>(
                     reinterpret_cast<header_type *>(start) + 1);
    // load accept
    start = accept_ = reinterpret_cast<accept_type *>(
                      reinterpret_cast<index_type *>(start)
                      + header_->index_size);
    // load front trie
    start = reinterpret_cast<accept_type *>(start) + header_->accept_size;
    lhs_ = new basic_trie(start,
                          reinterpret_cast<basic_trie::header_type *>(start)
                          + 1);
    // load rear trie
    start = reinterpret_cast<basic_trie::state_type *>
            ((basic_trie::header_type *)start + 1)
            + lhs_->header()->size;
    rhs_ = new basic_trie(start,
                          reinterpret_cast<basic_trie::header_type *>(start)
                          + 1);
}


double_trie::~double_trie()
{
    if (mmap_) {
        if (munmap(mmap_, mmap_size_) < 0)
            throw std::runtime_error(strerror(errno));
    } else {
        sanity_delete(header_);
        sanity_delete_array(index_);
        sanity_delete_array(accept_);
        sanity_delete(front_relocator_);
        sanity_delete(rear_relocator_);
    }
    sanity_delete(lhs_);
    sanity_delete(rhs_);
}

basic_trie::size_type
double_trie::rhs_append(const char *inputs, size_t length)
{
    const char *p = NULL;
    size_type s = 1, t;

    t = rhs_->next(s, basic_trie::kTerminator);
    if (rhs_->check_transition(s, t)) {
        s = rhs_->go_forward_reverse(t, inputs, length, &p);
        if (p < inputs) {  // all characters match
            t = rhs_->next(s, basic_trie::kTerminator);
            if (outdegree(s) == 0)
                return s;
            else if (rhs_->check_transition(s, t))
                return rhs_->next(s, basic_trie::kTerminator);
            else
                return rhs_->create_transition(s, basic_trie::kTerminator);
        }
    }
    if (outdegree(s) == 0) {
        t = rhs_->create_transition(s, basic_trie::kTerminator);
        std::set<size_type>::const_iterator it;
        for (it = refer_[s].referer.begin();
                it != refer_[s].referer.end();
                it++) {
            set_link(*it, t);
        }
        free_accept_entry(s);
    }
    if (s == 1) {
        p = inputs + length - 1;
        s = rhs_->create_transition(s, basic_trie::kTerminator);
    }
    for (; p >= inputs; p--) {
        t = rhs_->create_transition(s, basic_trie::char_in(*p));
        s = t;
    }
    return s;
}

basic_trie::size_type
double_trie::lhs_insert(size_type s, const char *inputs, size_t length)
{
    size_type t = lhs_->create_transition(s, basic_trie::char_in(inputs[0]));
    return set_link(t, rhs_append(inputs + 1, length - 1));
}

void double_trie::rhs_clean_more(size_type t)
{
    assert(t > 0);
    if (outdegree(t) == 0 && count_referer(t) == 0) {
        size_type s = rhs_->prev(t);
        remove_accept_state(t);
        if (s > 0)
            rhs_clean_more(s);
    } else if (outdegree(t) == 1) {
        size_type r = rhs_->next(t, basic_trie::kTerminator);
        if (rhs_->check_transition(t, r)) {
            // delete transition 't -#-> r'
            std::set<size_type>::const_iterator it;
            for (it = refer_[r].referer.begin();
                    it != refer_[r].referer.end();
                    it++)
                set_link(*it, t);
            accept_[refer_[t].accept_index].accept = t;
            remove_accept_state(r);
        }
    }
}

void double_trie::rhs_insert(size_type s, size_type r,
                      const char *match, size_t match_length,
                      const char *remain, size_t remain_length,
                      char ch, bool terminator, size_type value)
{
    size_type i;
    // R-1
    size_type u = link_state(s);  // u might be zero
    value_type oval = index_[-lhs_->base(s)].data;
    index_[-lhs_->base(s)].index = 0;
    index_[-lhs_->base(s)].data = 0;
    free_index_.push_back(-lhs_->base(s));
// XXX: check out the crash reason if base(s) is not set to zero.
    lhs_->set_base(s, 0);
    stand_ = r;
    if (u > 0) {
        refer_[u].referer.erase(s);

        if (refer_[u].referer.size() == 0)
            free_accept_entry(u);
    }

    // R-2
    const char *p;
    size_type t;
    for (p = match; p < match + match_length; p++) {
        t = lhs_->create_transition(s, basic_trie::char_in(*p));
        s = t;
    }
    if (remain_length > 0) {
        t = lhs_->create_transition(s, basic_trie::char_in(*remain));
        i = set_link(t, rhs_append(remain + 1, remain_length - 1));
        index_[i].data = value;
    } else {
        t = lhs_->create_transition(s, basic_trie::kTerminator);
        lhs_->set_base(t, value);
    }

    // R-3
    t = lhs_->create_transition(s, (terminator)?basic_trie::kTerminator:
                                                basic_trie::char_in(ch));
    size_type v = rhs_->prev(stand_);  // v -ch-> r
    if (!rhs_->check_transition(v, rhs_->next(v, basic_trie::kTerminator)))
        r = rhs_->create_transition(v, basic_trie::kTerminator);
    else
        r = rhs_->next(v, basic_trie::kTerminator);

    i = set_link(t, r);
    index_[i].data = oval;

    // R-4
    if (u > 0) {
        if (!rhs_clean_one(u))
            rhs_clean_more(u);
    }
}

void double_trie::insert(const char *inputs, size_t length, value_type value)
{
    if (!inputs)
        throw std::runtime_error("double_trie::insert: input pointer is null");

    size_type s, i;
    const char *p;

    s = lhs_->go_forward(1, inputs, length, &p);
    if (p < inputs + length && !check_separator(s)) {
        i = lhs_insert(s, p, length - (p - inputs));
        index_[i].data = value;
        return;
    } else if (!check_separator(s)) {
        size_type t = lhs_->next(s, basic_trie::kTerminator);
        if (lhs_->check_transition(s, t)) {
            if (check_separator(t))
                index_[-lhs_->base(t)].data = value;
            else
                index_[t].data = value;
        }
        return;
    }

    size_type r = link_state(s);
    if (rhs_->check_reverse_transition(r, basic_trie::kTerminator)
        && rhs_->prev(r) > 1)
        r = rhs_->prev(r);

    char last = 0;
    bool terminator = false;
    exists_.clear();
    for (; p < inputs + length; p++) {
        if (rhs_->check_reverse_transition(r, basic_trie::char_in(*p))) {
            r = rhs_->prev(r);
            exists_.append(1, *p);
        } else {
            break;
        }
    }
    // check for terminator
    if (p >= inputs + length &&
        rhs_->check_reverse_transition(r, basic_trie::kTerminator)) {
        r = rhs_->prev(r);
        exists_.append(1, basic_trie::kTerminator);
    } else {
        last = basic_trie::char_out(r - rhs_->base(rhs_->prev(r)));
        terminator = (r - rhs_->base(rhs_->prev(r))
                      == basic_trie::kTerminator)?true:false;
    }

    if (r > 1)
        rhs_insert(s, r, exists_.c_str(), exists_.length(),
                   p, length - (p - inputs), last, terminator, value);
    else
        index_[-lhs_->base(s)].data = value;

    return;
}

bool double_trie::search(const char *inputs,
                         size_t length,
                         value_type *value) const
{
    if (!inputs)
        throw std::runtime_error("basic_trie::search: input pointer is null");

    size_type s;
    const char *p;
    s = lhs_->go_forward(1, inputs, length, &p);
    if (p < inputs + length && !check_separator(s))
        return false;
    if (p >= inputs + length) {
        size_type t = lhs_->next(s, basic_trie::kTerminator);
        if (lhs_->check_transition(s, t)) {
            if (value)
                *value = (check_separator(t))?index_[-lhs_->base(t)].data:
                                              lhs_->base(t);
            return true;
        } else if (!check_separator(s)) {
            return false;
        }
    }
    size_type r = link_state(s);
    if (rhs_->check_reverse_transition(r, basic_trie::kTerminator))
        r = rhs_->prev(r);

    r = rhs_->go_backward(r, p, length - (p - inputs), NULL);
    r = rhs_->prev(r);
    if (r == 1) {
        if (value)
            *value = index_[-lhs_->base(s)].data;
        return true;
    }
    return false;
}

void double_trie::build(const char *filename, bool verbose)
{
    FILE *out;

    if (!filename)
        throw std::runtime_error(std::string("can not save to file ")
                                 + filename);

    if ((out = fopen(filename, "w+"))) {
        header_->index_size = next_index_;
        header_->accept_size = next_accept_;
        fwrite(header_, sizeof(header_type), 1, out);
        fwrite(index_, sizeof(index_type) * header_->index_size, 1, out);
        fwrite(accept_, sizeof(accept_type) * header_->accept_size, 1, out);
        fwrite(lhs_->header(), sizeof(basic_trie::header_type), 1, out);
        fwrite(lhs_->states(), sizeof(basic_trie::state_type)
                               * lhs_->header()->size, 1, out);
        fwrite(rhs_->header(), sizeof(basic_trie::header_type), 1, out);
        fwrite(rhs_->states(), sizeof(basic_trie::state_type)
                               * rhs_->header()->size, 1, out);
        fclose(out);
        if (verbose) {
            char buf[256];
            size_t size[4];
            size[0] = sizeof(index_type) * header_->index_size;
            size[1] = sizeof(accept_type) * header_->accept_size;
            size[2] = sizeof(basic_trie::state_type) * lhs_->header()->size;
            size[3] = sizeof(basic_trie::state_type) * rhs_->header()->size;

            fprintf(stderr, "index = %s, ",
                    pretty_size(size[0], buf, sizeof(buf)));
            fprintf(stderr, "accept = %s, ",
                    pretty_size(size[1], buf, sizeof(buf)));
            fprintf(stderr, "front = %s, ",
                    pretty_size(size[2], buf, sizeof(buf)));
            fprintf(stderr, "rear = %s, ",
                    pretty_size(size[3], buf, sizeof(buf)));
            fprintf(stderr, "total = %s\n",
                    pretty_size(size[0] + size[1] + size[2] + size[3],
                                buf, sizeof(buf)));
        }
    }
}

// ************************************************************************
// * Implementation of suffix trie                                        *
// ************************************************************************

suffix_trie::suffix_trie()
    :trie_(NULL), suffix_(NULL), header_(NULL), next_suffix_(1),
     mmap_(NULL), mmap_size_(NULL)
{
    trie_ = new basic_trie();
    header_ = new header_type();
    memset(&common_, 0, sizeof(common_));
    resize_suffix(256);
}

suffix_trie::suffix_trie(const char *filename)
    :trie_(NULL), suffix_(NULL), header_(NULL), next_suffix_(1),
     mmap_(NULL), mmap_size_(NULL)
{
    struct stat sb;
    int fd, retval;

    if (!filename)
        throw std::runtime_error(std::string("can not load from file ")
                                 + filename);

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        throw std::runtime_error(strerror(errno));
    if (fstat(fd, &sb) < 0)
        throw std::runtime_error(strerror(errno));

    mmap_ = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmap_ == MAP_FAILED)
        throw std::runtime_error(strerror(errno));
    while (retval = close(fd), retval == -1 && errno == EINTR) {
        // exmpty
    }
    mmap_size_ = sb.st_size;

    void *start;
    start = header_ = reinterpret_cast<header_type *>(mmap_);
    if (strcmp(header_->magic, magic_))
        throw std::runtime_error("file corrupted");
    // load suffix
    suffix_ = reinterpret_cast<suffix_type *>(
              reinterpret_cast<header_type *>(start) + 1);
    // load trie
    start = suffix_ + header_->suffix_size;
    trie_ = new basic_trie(start,
                          reinterpret_cast<basic_trie::header_type *>(start)
                          + 1);
}


suffix_trie::~suffix_trie()
{
    if (!mmap_) {
        sanity_delete(header_);
        sanity_delete_array(suffix_);
        sanity_delete_array(common_.data);
    }
    sanity_delete(trie_);
}

void suffix_trie::insert_suffix(size_type s,
                                const char *inputs,
                                size_t length,
                                value_type value)
{
    const char *p;

    trie_->set_base(s, -next_suffix_);
    if (next_suffix_ + length + 1 >= header_->suffix_size)
        resize_suffix(next_suffix_ + length + 1);

    for (p = inputs; p < inputs + length; p++)
        suffix_[next_suffix_++] = basic_trie::char_in(*p);
    suffix_[next_suffix_++] = basic_trie::kTerminator;
    suffix_[next_suffix_++] = value;
}

void suffix_trie::branch(size_type s,
                         const char *inputs,
                         size_t length,
                         value_type value)
{
    size_type suffix_start = -trie_->base(s);
    const char *p;
    char_type *cp;
    basic_trie::extremum_type extremum = {0, 0};

    // find common string
    if (length + 1 >= common_.size)
        resize_common(length + 1);
    for (p = inputs, cp = common_.data; p < inputs + length
                    && suffix_[suffix_start + p - inputs] 
                       == basic_trie::char_in(*p); p++) {
       *cp = basic_trie::char_in(*p);

       if (*p > extremum.max || !extremum.max)
           extremum.max = *cp;
       if (*p < extremum.min || !extremum.min)
           extremum.min = *cp;
       ++cp;    
    }
    *cp = '\0'; 
    
    if (p >= inputs + length
        && suffix_[suffix_start + p - inputs] == basic_trie::kTerminator) {
        suffix_[suffix_start + p - inputs + 1] = value;
    }

    // insert common string into trie
    size_type t = s;
    trie_->set_base(s, trie_->find_base(common_.data, extremum));
    for (cp = common_.data; *cp; cp++)
        t = trie_->create_transition(t, *cp);
    // create twig for old suffix
    s = trie_->create_transition(t, *(suffix_ + suffix_start + (p - inputs)));
    trie_->set_base(s, -(suffix_start + p - inputs + 1));
    // create twig for new suffix
    if (p < inputs + length)
        s = trie_->create_transition(t, basic_trie::char_in(*p));
    else
        s = t;
    insert_suffix(s, p + 1, length - (p + 1 - inputs), value);
}


void suffix_trie::insert(const char *inputs, size_t length, value_type val)
{
    if (!inputs)
        throw std::runtime_error("suffix_trie::insert: input pointer is null");

    size_type s;
    const char *p;

    s = trie_->go_forward(1, inputs, length, &p);

    if (trie_->base(s) < 0) {
        branch(s, p, length - (p - inputs), val);
    } else {
        if (p < inputs + length)
            s = trie_->create_transition(s, basic_trie::char_in(*p));
        insert_suffix(s, p + 1, length - (p + 1 - inputs), val);
    }
}

bool
suffix_trie::search(const char *inputs, size_t length, value_type *value) const
{
    if (!inputs)
        throw std::runtime_error("suffix_trie::search: input pointer is null");

    size_type s;
    const char *p;

    s = trie_->go_forward(1, inputs, length, &p);
    if (trie_->base(s) > 0) {
        size_type t;
        t = trie_->next(s, basic_trie::kTerminator);
        if (trie_->check_transition(s, t))
            s = t;
        else
            return false;
    }    
    if (trie_->base(s) < 0) {
        size_type len = length - (p - inputs);
        size_type start;

        for (start = -trie_->base(s); p < inputs + length; p++, start++) {
            if (basic_trie::char_in(*p) != suffix_[start])
                return false;
        }
        if (value) {
            if (suffix_[-trie_->base(s) + len] == basic_trie::kTerminator)
                *value = suffix_[-trie_->base(s) + len + 1];
            else
                *value = suffix_[-trie_->base(s)];
        }
        return true;
    } else {
        return false;
    }
}

void suffix_trie::build(const char *filename, bool verbose)
{
    FILE *out;

    if (!filename)
        throw std::runtime_error(std::string("can not save to file ")
                                 + filename);

    if ((out = fopen(filename, "w+"))) {
        snprintf(header_->magic, sizeof(header_->magic), "%s", magic_);
        header_->suffix_size = next_suffix_ - 1;
        fwrite(header_, sizeof(header_type), 1, out);
        fwrite(suffix_, sizeof(suffix_type) * header_->suffix_size, 1, out);
        fwrite(trie_->header(), sizeof(basic_trie::header_type), 1, out);
        fwrite(trie_->states(), sizeof(basic_trie::state_type)
                               * trie_->header()->size, 1, out);
                               
        fclose(out);
        if (verbose) {
            char buf[256];
            size_t size[2];
            size[0] = sizeof(suffix_type) * header_->suffix_size;
            size[1] = sizeof(basic_trie::state_type) * trie_->header()->size;

            fprintf(stderr, "suffix = %s, ",
                    pretty_size(size[0], buf, sizeof(buf)));
            fprintf(stderr, "trie = %s, ",
                    pretty_size(size[1], buf, sizeof(buf)));
            fprintf(stderr, "total = %s\n",
                    pretty_size(size[0] + size[1], buf, sizeof(buf)));
        }
    }
}
// vim: ts=4 sw=4 ai et
