Thread 9 Crashed:
0   0x000000010a71d0ac boost::cmt::rtask<void>::run() + 76
1   0x000000010a840d10 boost::cmt::thread::exec_fiber() + 964
2   0x000000010a84ae19 boost::_mfi::mf0<void, boost::cmt::thread>::operator()(boost::cmt::thread*) const + 89
3   0x000000010a84a8d6 void boost::_bi::list1<boost::_bi::value<boost::cmt::thread*> >::operator()<boost::_mfi::mf0<void, boost::cmt::thread>, boost::_bi::list0>(boost::_bi::type<void>, boost::_mfi::mf0<void, boost::cmt::thread>&, boost::_bi::list0&, int) + 74
4   0x000000010a8499fe boost::_bi::bind_t<void, boost::_mfi::mf0<void, boost::cmt::thread>, boost::_bi::list1<boost::_bi::value<boost::cmt::thread*> > >::operator()() + 60
5   0x000000010a84b238 boost::contexts::detail::context_object<boost::_bi::bind_t<void, boost::_mfi::mf0<void, boost::cmt::thread>, boost::_bi::list1<boost::_bi::value<boost::cmt::thread*> > >, boost::contexts::protected_stack>::exec() + 30
6   0x000000010a84a6de void boost::contexts::detail::trampoline<boost::contexts::detail::context_base<boost::contexts::protected_stack> >(void*) + 91
7   0x000000010a894634 boost_fcontext_make + 52
