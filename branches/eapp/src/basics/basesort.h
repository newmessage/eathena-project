#ifndef __BASESORT_H__
#define __BASESORT_H__

#include "basetypes.h"
#include "baseswap.h"

NAMESPACE_BEGIN(basics)

///////////////////////////////////////////////////////////////////////////////
/// test function
///////////////////////////////////////////////////////////////////////////////
void test_sort(int scale=1);



///////////////////////////////////////////////////////////////////////////////
/// quick sort defines
///////////////////////////////////////////////////////////////////////////////
#define QUICK_MEDIAN_OF_THREE	// activates median-of-three pivot selection
//#define QUICK_ONESIDE			// activates one-side-checking middle pivot selection
// both options off				// activates middle pivot selection

#define QUICK_STACKDEPTH	16	// depth of nonrecursive quicksort stack
#define QUICK_SHORTSWITCH	32	// size of small fields for switching to insertionsort


///////////////////////////////////////////////////////////////////////////////
/// Sort Algo's 
/// C-Array Implementation
/// working on standard fields
/// field objects need boolean compare operators
///////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
/// Bubble Sort, just informative, practically unusable
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void BubbleSort(T *a, ssize_t l, ssize_t r)
{
	if(r>l)
	{
		T *ip, *kp;
		for(ip=a+r; ip>a; --ip)
		for(kp=a+l; kp<ip; ++kp)
			if(kp[1] < kp[0]) swap(kp[0], kp[1]);
	}
}
template <typename T>
void BubbleSort(T *a, size_t count)
{
	if(count>1)
	{
		T *ip, *kp;
		for(ip=a+count-1; ip>a; --ip)
		for(kp=a; kp<ip; ++kp)
			if(kp[1] < kp[0]) swap(kp[0], kp[1]);
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Insertion Sort, linear time for small fields
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void InsertionSort(T *a, ssize_t l, ssize_t r)
{
	T *epr=a+r, *elp=a+l;
	T *ip, *kp;
	T v;
	for(ip=elp+1; ip<=epr; ++ip)
	{
		for(kp=ip-1, v=*ip; kp>=elp && v<*kp; kp[1]=kp[0], --kp);
		kp[1] = v;
	}
}

template <typename T>
void InsertionSort(T *a, size_t count)
{
	const T *ep=a+count;
	T *ip, *kp;
	T v;
	for(ip=a+1; ip<ep; ++ip)
	{
		for(kp=ip-1, v=*ip; kp>=a && v<*kp; kp[1]=kp[0], --kp);
		kp[1] = v;
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Selection Sort
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void SelectionSort(T *a, ssize_t l, ssize_t r)
{
	T *ip, *jp, *minp, *epr=a+r;
	for(ip=a+l; ip<epr; ++ip)
	{
		for(minp=ip, jp=ip+1; jp<=epr; ++jp) if(*jp < *minp) minp = jp;
		swap(*minp, *ip);
	}
}
template <typename T>
void SelectionSort(T *a, size_t count)
{
	T *ip, *jp, *minp, *ep=a+count-1;
	for(ip=a; ip<ep; ++ip)
	{
		for(minp=ip, jp=ip+1; jp<=ep; ++jp) if(*jp < *minp) minp = jp;
		swap(*minp, *ip);
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Shell Sort
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void ShellSort(T *a, ssize_t l, ssize_t r)
{
	ssize_t h;
	T *ip, *jp, *kp, *ep=a+r;
	T v;
	for(h=1; h<=(r-l)/9; h=3*h+1);
	for(; h>0; h/=3)
	{
		for(ip=a+h; ip<=ep; ++ip)
		{
			for(kp=ip, jp=ip-h, v=*kp; jp>=a && v<*jp; *kp=*jp, kp=jp, jp-=h);
			*kp = v;
		}
	}
}

template <typename T>
void ShellSort(T *a, size_t count)
{
	size_t h;
	T *ip, *jp, *kp, *ep=a+count;
	T v;
	for(h=1; h<=count/9; h=3*h+1);
	for(; h>0; h/=3)
	{
		for(ip=a+h; ip<ep; ++ip)
		{
			for(kp=ip, jp=ip-h, v=*kp; jp>=a && v<*jp; *kp=*jp, kp=jp, jp-=h);
			*kp = v;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Merge Sort, stable variant of quicksort but needs extra memory and copies
///////////////////////////////////////////////////////////////////////////////
// Array b must have same size as a; result is in a!
template <typename T>
void MergeSort(T *a, T *b, ssize_t l, ssize_t r)
{
	if(r > l)
	{
		T *ip, *jp, *kp, *ep;
		size_t m = (r+l)/2;
		MergeSort<T>(a, b, l, m);			// takes m+1 elements (0...m)
		MergeSort<T>(a, b, m+1, r);			// takes c-m-1 elements (m+1...c-1)
		for(ip=a+l,   ep=a+m, jp=b+l;         ip<=ep; ++ip, ++jp) *jp = *ip;
		for(ip=a+m+1, ep=a+r, jp=b+r;         ip<=ep; ++ip, --jp) *jp = *ip;
		for(kp=a+l,   ep=a+r, ip=b+l, jp=b+r; kp<=ep; ++kp)       *kp = (*ip < *jp) ?  *ip++ : *jp--;
	}

}

// Array b must have same size like a; result is in a!
template <typename T>
void MergeSort(T *a, T *b, size_t count)
{
	if(count>1)
	{
		T *ip, *kp, *mp, *ep;
		size_t m = count/2;
		MergeSort<T>(a+0, b+0, m);			// takes m elements (0...m-1)
		MergeSort<T>(a+m, b+m, count-m);	// takes c-m elements (m...c-1)
		for(ip=a+0, ep=a+m,     kp=b;               ip<ep; ++ip, ++kp) *kp = *ip;
		for(ip=a+m, ep=a+count, kp=b+count-1;       ip<ep; ++ip, --kp) *kp = *ip;
		for(ip=a+0, ep=a+count, kp=b, mp=b+count-1; ip<ep; ++ip)       *ip = (*kp < *mp) ?  *kp++ : *mp--;
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Comb Sort, variant of BubbleSort with variable gap between compare positions
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void CombSort(T *a, ssize_t l, ssize_t r)
{
	if(r>l)
	{
		T *ip, *jp, *ep;
		size_t gap = 1+r-l;
		bool swapped;
		for (;;)
		{
			gap = (gap * 10) / 13;
			if (gap == 9 || gap == 10)
				gap = 11;
			else if (gap < 1)
				gap = 1;
			swapped = false;
			for(ip=a+l, jp=ip+gap, ep=a+r-gap; ip<=ep; ++ip,++jp)
			{
				if(*jp < *ip)
				{
					swap(*ip, *jp);
					swapped = true;
				}
			}
			if(gap==1 && !swapped)
				break;
		}
	}
}

template <typename T>
void CombSort(T *a, size_t count)
{
	if(count > 1)
	{
		T *ip, *jp, *ep;
		size_t gap = count;
		bool swapped;
		for (;;)
		{
			gap = (gap * 10) / 13;
			if (gap == 9 || gap == 10)
				gap = 11;
			else if (gap < 1)
				gap = 1;
			swapped = false;
			for(ip=a, jp=ip+gap, ep=a+count-gap; ip<ep; ++ip,++jp)
			{
				if(*jp < *ip)
				{
					swap(*ip, *jp);
					swapped = true;
				}
			}
			if(gap==1 && !swapped)
				break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Heap Sort, builds a binary tree
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void HeapSort(T *a, ssize_t l, ssize_t r)
{
	if(r>l)
	{
		ssize_t n = 1+r;
		ssize_t i = l+(n-l)/2;
		ssize_t parent, child;
		T t;
		for (;;)
		{
			if(i>l)
			{
				i--;
				t = a[i];
			}
			else
			{
				n--;
				if (n == l)
					break;
				t = a[n];
				a[n] = a[l];
			}
			parent = i;
			child = 2*i-l+1;
			while (child < n)
			{
				if( child+1 < n && a[child] < a[child+1] )
				{
					child++;
				}
				if( t < a[child] )
				{
					a[parent] = a[child];
					parent = child;
					child = 2*parent-l+1;
				}
				else
					break;
			}
			a[parent] = t;
		}
	}
}
template <typename T>
void HeapSort(T *a, size_t count)
{
	if(count>1)
	{
		size_t n = count;
		size_t i = count/2;
		size_t parent, child;
		T t;
		for (;;)
		{
			if(i>0)
			{
				i--;
				t = a[i];
			}
			else
			{
				n--;
				if (n == 0)
					break;
				t = a[n];
				a[n] = a[0];
			}
			parent = i;
			child = i*2+1;
			while (child < n)
			{
				if( child+1 < n  && a[child] < a[child+1] )
				{
					child++;
				}
				if( t < a[child] )
				{
					a[parent] = a[child];
					parent = child;
					child = parent*2 + 1;
				}
				else
					break;
			}
			a[parent] = t;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Heap Sort variant.
/// builds heap from bottom up, using downheaps
///////////////////////////////////////////////////////////////////////////////
template<typename T>
void HeapSortBUDH(T *a, ssize_t l, ssize_t r)
{
	if(r>l)
	{
		T x;
		ssize_t u, v, w, n=r;
		// build heap
		for(u=v=(l+r+1)/2; v>=l; u=--v)
		{	// downheap;
			w=2*v-l+1;					// first descendant of v
			while( w<=r )
			{
				if (w<r)				// is there a second descendant?
					if( a[w]<a[w+1]) w++;
					// w is the descendant of v with maximum label
				if (a[u]<a[w])
				{
					swap(a[u], a[w]);	// exchange labels of v and w
					u=w;				// continue
					w=2*u-l+1;
				}
				else
					break;				// v has heap property
			}
		}
		// sort
		while( n>l )
		{
			x=a[n];    // label of last leaf 
			a[n]=a[l];
			n--;
			
			// holedownheap
			v=l, w=2*v-l+1;
			while(w<n)     // second descendant exists
			{
				if(a[w]<a[w+1]) w++;
				// w is the descendant of v with maximum label
				a[v]=a[w];
				v=w; 
				w=2*v-l+1;
			}
			if (w<=n)    // single leaf
			{
				a[v]=a[w];
				v=w;
			}
			// hole has reached leaf, leaf is returned
			// upheap
			a[v]=x;
			while (v>l)
			{
				u=(v+l+1)/2-1;    // predecessor
				if( a[u]<a[v] )
				{
					swap(a[u], a[v]);
					v=u;
				}
				else
					break;
			}
		}
	}
}
template<typename T>
void HeapSortBUDH(T *a, size_t count)
{
	if(count>1)
	{
		T x;
		size_t u, w, n=count;
		ssize_t v;

		// build heap
		for(u=v=count/2-1; v>=0; u=--v)
		{	// downheap;
			w=2*v+1;					// first descendant of v
			while (w<count)
			{
				if( w<count-1 )			// is there a second descendant?
					if(a[w]<a[w+1]) w++;
					// w is the descendant of v with maximum label
				if (a[u]<a[w])
				{
					swap(a[u], a[w]);	// exchange labels of v and w
					u=w;				// continue
					w=2*u+1;
				}
				else
					break;				// v has heap property
			}
		}
		// sort
		while (n>1)
		{
			n--;
			x=a[n];    // label of last leaf 
			a[n]=a[0];
			
			// holedownheap
			v=0, w=2*v+1;
			while(w+1<n)     // second descendant exists
			{
				if(a[w]<a[w+1]) w++;
				// w is the descendant of v with maximum label
				a[v]=a[w];
				v=w; 
				w=2*v+1;
			}
			if (w<n)    // single leaf
			{
				a[v]=a[w];
				v=w;
			}
			// hole has reached leaf, leaf is returned
			// upheap
			a[v]=x;
			while (v>0)
			{
				u=(v+1)/2-1;    // predecessor
				if (a[u]<a[v]) 
				{
					swap(a[u], a[v]);
					v=u;
				}
				else
					break;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Heap Sort variant.
/// builds heap from bottom up, using upheaps
///////////////////////////////////////////////////////////////////////////////
/*
// derived from Written by J. Teuhola <teuhola@cs.utu.fi>
// http://www.diku.dk/hjemmesider/ansatte/jyrki/Experimentarium/in-place_linear_probing_sort/heapsort.bottom-up.c
// http://www.diku.dk/hjemmesider/ansatte/jyrki/Experimentarium/
//
// 
template<typename T>
void siftup(T *v, int i, int n)
{
	int j, start;
	T x;
	
	start = i;
	x = v[i];
	j = i<<1;
	while (j<=n)
	{
		if (j<n)
			if (v[j]<v[j+1])
				j++;
			v[i] = v[j];
			i = j; j = i<<1;
	}
	j = i>>1;
	while (j>=start)
	{
		if (v[j]<x)
		{
			v[i] = v[j];
			i = j; j = i>>1;
		}
		else
			break;
	}
	v[i] = x;
}

//----------------------------------------------------------------------
// The heapsort procedure; the original array is r[0..n-1], but here
// it is shifted to vector v[1..n], for convenience.
//----------------------------------------------------------------------
template<typename T>
void bottom_up_heapsort(T a[], int n)
{
	int k;
	T x;
	T*v;
	
	v = a-1;
	for (k=n>>1; k>1; k--)
		siftup(v, k, n);
	
	for (k=n; k>1; k--)
	{
		siftup(v, 1, k);
		x = v[k]; v[k] = v[1]; v[1] = x;
	}
}
*/
template<typename T>
void HeapSortBUUH(T *a, ssize_t l, ssize_t r)
{
	if(r>l)
	{
		ssize_t i,j,k;
		T x;
		// Build the heap bottom-up, using shiftup.
		for(i=k=(l+r-1)/2; k>l; i=--k)
		{
			x = a[i];
			/*
			j = 2*i-l+1;
			while( j<=r )
			{
				if( j<r )
					if( a[j]<a[j+1] ) j++;
				a[i] = a[j];
				i = j; 
				j = 2*i-l+1;
			}
			*/
			///////////////
			j = 2*i-l+2;
			while( j<=r )
			{					// get rid of the compare in the inner loop
				if( a[j]<a[j-1] ) --j;
				a[i] = a[j];
				i = j; 
				j = 2*i-l+2;
			}
			if(j==r+1)			// and set it up outside with some extra code
			{
				a[i] = a[r];
				i = r; 
			}
			///////////////
			j = (i+l+1)/2-1;
			while (j>=k)
			{
				if (a[j]<x)
				{
					a[i] = a[j];
					i = j;
					j = (i+l+1)/2-1;
				}
				else
					break;
			}
			a[i] = x;
		}
		// The main loop of sorting follows. 
		// The root is swapped with the last leaf after each shift-up.
		for (k=r; k>l; --k)
		{
			i = l;
			x = a[i];
			/*
			j = 2*i-l+1;
			while( j<=k )
			{
				if( j<k )
					if( a[j]<a[j+1] ) j++;
				a[i] = a[j];
				i = j; 
				j = 2*i-l+1;
			}
			*/
			///////////////
			j = 2*i-l+2;
			while( j<=k )
			{					// get rid of the compare in the inner loop
				if( a[j]<a[j-1] ) --j;
				a[i] = a[j];
				i = j; 
				j = 2*i-l+2;
			}
			if(j==k+1)			// and set it up outside with some extra code
			{
				a[i] = a[k];
				i = k; 
			}
			///////////////
			j = (i+l+1)/2-1;
			while (j>=l)
			{
				if( a[j]<x )
				{
					a[i] = a[j];
					i = j;
					j = (i+l-1)/2;
				}
				else
					break;
			}
			a[i] = x;

			// swap heap top
			x = a[k]; 
			a[k] = a[l];
			a[l] = x;
		}
	}
}


template<typename T>
void HeapSortBUUH(T *a, size_t count)
{
	if(count>1)
	{
		ssize_t i,j,k;
		T x;
		// Build the heap bottom-up, using shiftup.
		// don't build it completely, let root element untouched
		for(i=k=count/2-1; k>0; i=--k)
		{
			x = a[i];
			///////////////
			j = 2*i+2;					// instead of going to the first child with [j = 2*i+1;], go to the second
			while( (size_t)j<count )
			{	//if( j<k )				// and get rid of the compare in the inner loop
				if( a[j]<a[j-1] ) --j;	// but modify the compare [if( a[j]<a[j+1] ) j++;]
				a[i] = a[j];
				i = j; 
				j = 2*i+2;				// instead of going to the first child with [j = 2*i+1;], go to the second
			}
			if((size_t)j==count)		// going to the second child also breaks the while on single childed end nodes
			{							// and this will need to set up some extra code
				a[i] = a[count-1];
				i = count-1; 
			}
			///////////////
			// simplify some calculation
			j = (i-1)/2;				// instead of [j = (i+1)/2-1;] since k is >0
			while(j>=k)					// the compare can be the same
			{
				if( a[j]<x )
				{
					a[i] = a[j];
					i = j;
					j = (i-1)/2;		// instead of [j = (i+1)/2-1;] since k is >0
				}
				else
					break;
			}
			///////////////
			a[i] = x;
		}
		// The main loop of sorting follows. 
		// The root is swapped with the last leaf after each shift-up.
		for (k=count-1; k>0; --k)
		{
			i = 0;
			x = a[i];
			///////////////
			j = 2*i+2;					// instead of going to the first child with [j = 2*i+1;], go to the second
			while( j<=k )
			{	
				//if( j<k )				// and get rid of the compare in the inner loop
				if( a[j]<a[j-1] ) --j;	// but modify the compare [if( a[j]<a[j+1] ) j++;]
				a[i] = a[j];
				i = j; 
				j = 2*i+2;				// instead of going to the first child with [j = 2*i+1;], go to the second
			}
			if(j==k+1)					// going to the second child also breaks the while on single childed end nodes
			{							// and this will need to set up some extra code
				a[i] = a[k];
				i = k; 
			}
			///////////////
			j = (i-1)/2;				// do instead of [j = (i+1)/2-1;]
			while(i>0)					// but then check with i>0 instead of j>=0
			{
				if( a[j]<x )
				{
					a[i] = a[j];
					i = j;
					j = (i-1)/2;		// do instead of [j = (i+1)/2-1;]
				}
				else
					break;
			}
			///////////////
			a[i] = x;

			// swap heap top
			x = a[k]; 
			a[k] = a[0];
			a[0] = x;
		}
	}
}



///////////////////////////////////////////////////////////////////////////////
/// classic QuickSort.
/// modified with middle pivot 
/// to not get worst case behaviour on already sorted fields
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void QuickSortClassic_(T *a, ssize_t l, ssize_t r)
{
	if(r > l)
	{
		T* ip = a+l-1;
		T* jp = a+r;
		T* rp = jp;
		// always take middle pivot
		// this makes perfect match for sorted lists
		if(l+3<r) swap(a[(l+r)/2], *rp);
		for(;;)
		{
			while( *(++ip) < *rp);				// move upwards as long as smaller
			while( *rp < *(--jp));				// move down as long as larger
			if(ip >= jp) break;					// finish when pointers crossed
			swap(*ip, *jp);						// swap larger and smaller
		}
		swap(*ip, *rp);							// swap pivot in place
		QuickSortClassic_(a, l, (ip-a)-1);		// partition before a[i]
		QuickSortClassic_(a, (ip-a)+1, r);		// partition after a[i]
	}
}
template <typename T>
void QuickSortClassic(T *a, size_t count)
{
	if(count>1)
	{
		T* ip = a-1;
		T* jp = a+count-1;
		T* rp = jp;
		// always take middle pivot
		// this makes perfect match for sorted lists
		if(count>=3) swap(a[count/2], *rp);
		for(;;)
		{
			while( *(++ip) < *rp);				// move upwards as long as smaller
			while( *rp < *(--jp));				// move down as long as larger
			if(ip >= jp) break;					// finish when pointers crossed
			swap(*ip, *jp);						// swap larger and smaller
		}
		swap(*ip, *rp);							// swap pivot in place
		QuickSortClassic<T>(a,   ip-a);			// partition before a[i]
		QuickSortClassic<T>(ip+1,rp-ip);		// partition after a[i]
	}
}
template <typename T, typename Compare>
void QuickSortClassic(T *a, size_t count, const Compare& cmp)
{
	if(count>1)
	{
		T* ip = a-1;
		T* jp = a+count-1;
		T* rp = jp;
		// always take middle pivot
		// this makes perfect match for sorted lists
		if(count>=3) swap(a[count/2], *rp);
		for(;;)
		{
			while( cmp(*(++ip),*rp) );			// move upwards as long as smaller
			while( cmp(*rp,*(--jp)) );			// move down as long as larger
			if(ip >= jp) break;					// finish when pointers crossed
			swap(*ip, *jp);						// swap larger and smaller
		}
		swap(*ip, *rp);							// swap pivot in place
		QuickSortClassic(a,   ip-a, cmp);		// partition before a[i]
		QuickSortClassic(ip+1,rp-ip, cmp);		// partition after a[i]
	}
}
///////////////////////////////////////////////////////////////////////////////
/// nonrecursive QuickSort with definable pivot selection.
/// switching to InsertionSort for short partitions (<=32 elements)
/// and switching to HeapSort, when stack is full
///////////////////////////////////////////////////////////////////////////////
template <typename T>
void QuickSort(T *a, ssize_t l, ssize_t r)
{
	if(r>l)
	{
		T *stack[2*QUICK_STACKDEPTH], **sp=stack, **se=stack+sizeof(stack)/sizeof(stack[0]);
		T *lp=a+l, *rp=a+r;
		T *ip, *jp;
#if defined(QUICK_MEDIAN_OF_THREE) || defined(QUICK_ONESIDE)
		T *cp;
#endif
		for(;;)
		{
			if(lp+QUICK_SHORTSWITCH < rp)
			{	
#if defined(QUICK_MEDIAN_OF_THREE) 
				//Median of three, no check for (r-l > 3) because always true
				cp = lp + (rp-lp)/2;
				if(*cp < *lp)
					swap(*lp, *cp);
				if(*rp < *lp)
					swap(*lp, *rp);
				else if( *cp < *rp)
					swap(*rp, *cp);
#elif defined(QUICK_ONESIDE)
				// always middle pivot
				// with one side precheck
				cp = lp + (rp-lp)/2;
				if(*cp < *rp)
					swap(*rp, *cp);
#else
				// without precheck
				swap(*rp, lp[(rp-lp)/2]);
#endif

				ip = lp-1; jp = rp;
				for(;;)
				{
					while( *(++ip) < *rp);
					while( *rp < *(--jp));
					if(ip >= jp) break;
					swap(*ip, *jp);
				}
				swap(*ip, *rp);
				if( sp<se)
				{	// put the larger partition on the stack
					if( ip-lp > rp-ip )
					{
						*sp++=lp;
						*sp++=ip-1;
						lp=ip+1;
					}
					else
					{
						*sp++=ip+1;
						*sp++=rp;
						rp=ip-1;
					}
				}
				else
				{	// switch to heapsort if internal stack is full
					HeapSort<T>(lp,   ip-lp); // choose the faster heapsort
					HeapSort<T>(ip+1, rp-ip); // choose the faster heapsort
					if(sp>stack)
					{
						rp=*(--sp);
						lp=*(--sp);
					}
					else
						break;
				}
			}
			else
			{	// do InsertionSort on small partitions
				InsertionSort<T>(lp, rp-lp+1);
				if(sp>stack)
				{
					rp=*(--sp);
					lp=*(--sp);
				}
				else
					break;
			}

		}
	}
}

template <typename T>
void QuickSort(T *a, size_t count)
{
	if(count>1)
	{
		T *stack[2*QUICK_STACKDEPTH], **sp=stack, **se=stack+sizeof(stack)/sizeof(stack[0]);
		T *lp=a, *rp=a+count-1;
		T *ip, *jp;
#if defined(QUICK_MEDIAN_OF_THREE) || defined(QUICK_ONESIDE)
		T *cp;
#endif
		for(;;)
		{
			if(lp+QUICK_SHORTSWITCH < rp)
			{	
#if defined(QUICK_MEDIAN_OF_THREE) 
				//Median of three, no check for (r-l > 3) because always true
				cp = lp + (rp-lp)/2;
				if(*cp < *lp)
					swap(*lp, *cp);
				if(*rp < *lp)
					swap(*lp, *rp);
				else if(*cp < *rp)
					swap(*rp, *cp);
#elif defined(QUICK_ONESIDE)
				// always middle pivot
				// with one side precheck
				cp = lp + (rp-lp)/2;
				if(*cp < *rp)
					swap(*rp, *cp);
#else
				// without precheck
				swap(*rp, lp[(rp-lp)/2]);
#endif

				ip = lp-1; jp = rp;
				for(;;)
				{
					while( *(++ip) < *rp);
					while( *rp < *(--jp));
					if(ip >= jp) break;
					swap(*ip, *jp);
				}
				swap(*ip, *rp);
				if( sp<se )
				{	// put the larger partition on the stack
					if( rp-ip < ip-lp )
					{
						*sp++=lp;
						*sp++=ip-1;
						lp=ip+1;
					}
					else
					{
						*sp++=ip+1;
						*sp++=rp;
						rp=ip-1;
					}
				}
				else
				{	// switch to heapsort if internal stack is full
					HeapSort<T>(lp,   ip-lp); // choose the faster heapsort
					HeapSort<T>(ip+1, rp-ip); // choose the faster heapsort
					if(sp>stack)
					{
						rp=*(--sp);
						lp=*(--sp);
					}
					else
						break;
				}
			}
			else
			{	// do InsertionSort on small partitions
				InsertionSort<T>(lp, rp-lp+1);
				if(sp>stack)
				{
					rp=*(--sp);
					lp=*(--sp);
				}
				else
					break;
			}
		}
	}
}


NAMESPACE_END(basics)

#endif//__BASESORT_H__


