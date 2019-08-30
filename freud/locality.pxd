# Copyright (c) 2010-2019 The Regents of the University of Michigan
# This file is from the freud project, released under the BSD 3-Clause License.

from libcpp cimport bool as cbool
from libcpp.memory cimport shared_ptr

from cython.operator cimport dereference

cimport freud._locality
cimport freud.box


cdef class NewResultClass:
    cdef NeighborQuery nq
    cdef const float[:, ::1] points
    cdef _QueryArgs query_args

    # This had to be implemented as a factory because the constructors will
    # always get called with Python objects as arguments, and we need typed
    # objects. See the link below for more information.
    # https://cython.readthedocs.io/en/latest/src/userguide/special_methods.html#initialisation-methods-cinit-and-init
    # This needed to be declared inline because it needed to be in the pxd,
    # which in turn was the only way to get the staticmethod decorator to
    # compile with a cdef method.
    @staticmethod
    cdef inline NewResultClass init(NeighborQuery nq, const float[:, ::1]
                                    points, _QueryArgs query_args):

        obj = NewResultClass()

        obj.nq = nq
        obj.points = points
        obj.query_args = query_args
        obj.nq.nqptr.validateQueryArgs(dereference(query_args.thisptr))

        return obj

cdef class NlistptrWrapper:
    cdef freud._locality.NeighborList * nlistptr
    cdef freud._locality.NeighborList * get_ptr(self)

cdef class NeighborQuery:
    cdef freud._locality.NeighborQuery * nqptr
    cdef cbool queryable
    cdef freud.box.Box _box
    cdef const float[:, ::1] points
    cdef freud._locality.NeighborQuery * get_ptr(self)

cdef class NeighborList:
    cdef freud._locality.NeighborList * thisptr
    cdef char _managed
    cdef base

    cdef refer_to(self, freud._locality.NeighborList * other)
    cdef freud._locality.NeighborList * get_ptr(self)
    cdef void copy_c(self, NeighborList other)

cdef class IteratorLinkCell:
    cdef freud._locality.IteratorLinkCell * thisptr

    cdef void copy(self, const freud._locality.IteratorLinkCell & rhs)

cdef class LinkCell(NeighborQuery):
    cdef freud._locality.LinkCell * thisptr
    cdef NeighborList _nlist

cdef class NearestNeighbors:
    cdef freud._locality.NearestNeighbors * thisptr
    cdef NeighborList _nlist
    cdef _cached_points
    cdef _cached_query_points
    cdef _cached_box

cdef class AABBQuery(NeighborQuery):
    cdef freud._locality.AABBQuery * thisptr
    cdef NeighborList _nlist

cdef class RawPoints(NeighborQuery):
    cdef freud._locality.RawPoints * thisptr

cdef class _QueryArgs:
    cdef freud._locality.QueryArgs * thisptr

cdef class _Voronoi:
    cdef freud._locality.Voronoi * thisptr
    cdef NeighborList _nlist
    cdef _volumes
    cdef _polytopes
