/* -----------------------------------------------------------------------------
 * typemap.c
 *
 *     A somewhat generalized implementation of SWIG1.1 typemaps.
 *
 * Author(s) : David Beazley (beazley@cs.uchicago.edu)
 *
 * Copyright (C) 1999-2000.  The University of Chicago
 * See the file LICENSE for information on usage and redistribution.
 * ----------------------------------------------------------------------------- */

static char cvsroot[] = "$Header$";

#include "swig.h"

/* -----------------------------------------------------------------------------
 * Typemaps are stored in a collection of nested hash tables.  Something like
 * this:
 *
 * [ type ]
 *    +-------- [ name ]
 *    +-------- [ name ]
 *    
 * Each hash table [ type ] or [ name ] then contains references to the
 * different typemap methods.    These are referenced by names such as
 * "tmap:in", "tmap:out", "tmap:argout", and so forth.
 *
 * The object corresponding to a specific method has the following
 * attributes:
 *
 *    "type"    -  Typemap type
 *    "pname"   -  Parameter name
 *    "code"    -  Typemap code
 *    "typemap" -  Descriptive text describing the actual map
 *    "locals"  -  Local variables (if any)
 * 
 * ----------------------------------------------------------------------------- */

#define MAX_SCOPE  32

static Hash *typemaps[MAX_SCOPE];
static int   tm_scope = 0;

/* -----------------------------------------------------------------------------
 * Swig_typemap_init()
 *
 * Initialize the typemap system
 * ----------------------------------------------------------------------------- */

void Swig_typemap_init() {
  int i;
  for (i = 0; i < MAX_SCOPE; i++) {
    typemaps[i] = 0;
  }
  typemaps[0] = NewHash();
  tm_scope = 0;
}

static String *tmop_name(const String_or_char *op) {
  static Hash *names = 0;
  String *s;
  if (!names) names = NewHash();
  s = Getattr(names,op);
  if (s) return s;
  s = NewStringf("tmap:%s",op);
  Setattr(names,op,s);
  return s;
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_new_scope()
 * 
 * Create a new typemap scope
 * ----------------------------------------------------------------------------- */

void Swig_typemap_new_scope() {
  tm_scope++;
  typemaps[tm_scope] = NewHash();
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_pop_scope()
 *
 * Pop the last typemap scope off
 * ----------------------------------------------------------------------------- */

Hash *
Swig_typemap_pop_scope() {
  if (tm_scope > 0) {
    return typemaps[tm_scope--];
  }
  return 0;
}

/* ----------------------------------------------------------------------------- 
 * Swig_typemap_register_multi()
 *
 * Add a new multi-valued typemap
 * ----------------------------------------------------------------------------- */

void
Swig_typemap_register_multi(const String_or_char *op, ParmList *parms, String_or_char *code, ParmList *locals, ParmList *kwargs) {
  Hash *tm;
  Hash *tm1;
  Hash *tm2;
  Parm *np;
  String *tmop;
  SwigType *type;
  String   *pname;

  if (!parms) return;
  tmop = tmop_name(op);

  /* Register the first type in the parameter list */

  type = Getattr(parms,"type");
  pname = Getattr(parms,"name");

  /* See if this type has been seen before */
  tm = Getattr(typemaps[tm_scope],type);
  if (!tm) {
    tm = NewHash();
    Setattr(typemaps[tm_scope],Copy(type),tm);
    Delete(tm);
  }
  if (pname) {
    /* See if parameter has been seen before */
    tm1 = Getattr(tm,pname);
    if (!tm1) {
      tm1 = NewHash();
      Setattr(tm,NewString(pname),tm1);
      Delete(tm1);
    }
    tm = tm1;
  }

  /* Now see if this typemap op has been seen before */
  tm2 = Getattr(tm,tmop);
  if (!tm2) {
    tm2 = NewHash();
    Setattr(tm,tmop,tm2);
    Delete(tm2);
  }

  /* For a multi-valued typemap, the typemap code and information
     is really only stored in the last argument.  However, to
     make this work, we perform a really neat trick using
     the typemap operator name.

     For example, consider this typemap

       %typemap(in) (int foo, int *bar, char *blah[]) {
            ...
       }

     To store it, we look at typemaps for the following:

          operator                  type-name
          ----------------------------------------------
          "in"                      int foo
          "in-int+foo:"             int *bar
          "in-int+foo:-p.int+bar:   char *blah[]

     Notice how the operator expands to encode information about
     previous arguments.        

  */

  np = nextSibling(parms);
  if (np) {
    /* Make an entirely new operator key */
    String *newop = NewStringf("%s-%s+%s:",op,type,pname);
    /* Now reregister on the remaining arguments */
    Swig_typemap_register_multi(newop,np,code,locals,kwargs);
    
    /*    Setattr(tm2,newop,newop); */
    Delete(newop);
  } else {
    Setattr(tm2,"code",NewString(code));
    Setattr(tm2,"type",Copy(type));
    Setattr(tm2,"typemap",NewStringf("typemap(%s) %s", op, SwigType_str(type,pname)));
    if (pname) {
      Setattr(tm2,"pname", NewString(pname));
    }
    Setattr(tm2,"locals", CopyParmList(locals));
    Setattr(tm2,"kwargs", CopyParmList(kwargs));
  }
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_get()
 *
 * Retrieve typemap information from current scope.
 * ----------------------------------------------------------------------------- */

static Hash *
Swig_typemap_get(SwigType *type, String_or_char *name, int scope) {
  Hash *tm, *tm1;
  /* See if this type has been seen before */
  if ((scope < 0) || (scope > tm_scope)) return 0;
  tm = Getattr(typemaps[scope],type);
  if (!tm) {
    return 0;
  }
  if ((name) && Len(name)) {
    tm1 = Getattr(tm, name);
    return tm1;
  }
  return tm;
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_copy()
 *
 * Copy a typemap
 * ----------------------------------------------------------------------------- */

int
Swig_typemap_copy_multi(const String_or_char *op, ParmList *srcparms, ParmList *parms) {
  Hash *tm = 0;
  String *tmop;
  Parm *p;
  String *pname;
  SwigType *ptype;
  int ts = tm_scope;
  String *tmops, *newop;
  if (Len(parms) != Len(srcparms)) return -1;

  tmop = tmop_name(op);
  while (ts >= 0) {
    p = srcparms;
    tmops = NewString(tmop);
    while (p) {
      ptype = Getattr(p,"type");
      pname = Getattr(p,"name");
      
      /* Lookup the type */
      tm = Swig_typemap_get(ptype,pname,ts);
      if (!tm) break;
      
      tm = Getattr(tm,tmops);
      if (!tm) break;

      /* Got a match.  Look for next typemap */
      newop = NewStringf("%s-%s+%s:",tmops,ptype,pname);
      Delete(tmops);
      tmops = newop;
      p = nextSibling(p);
    }
    Delete(tmops);
    if (!p && tm) {
      /* Got some kind of match */
      Swig_typemap_register_multi(op,parms, Getattr(tm,"code"), Getattr(tm,"locals"),Getattr(tm,"kwargs"));
      return 0;
    }
    ts--;
  }
  /* Not found */
  return -1;

}

/* -----------------------------------------------------------------------------
 * Swig_typemap_clear_multi()
 *
 * Delete a multi-valued typemap
 * ----------------------------------------------------------------------------- */

void
Swig_typemap_clear_multi(const String_or_char *op, ParmList *parms) {
  SwigType *type;
  String   *name;
  Parm     *p;
  String   *newop;
  Hash *tm = 0;

  /* This might not work */
  newop = NewString(op);
  p = parms;
  while (p) {
    type = Getattr(p,"type");
    name = Getattr(p,"name");
    tm = Swig_typemap_get(type,name,tm_scope);
    if (!tm) return;
    p = nextSibling(p);
    if (p) 
      Printf(newop,"-%s+%s:", type,name);
  }
  if (tm) {
    tm = Getattr(tm, tmop_name(newop));
    if (tm) {
      Delattr(tm,"code");
      Delattr(tm,"locals");
      Delattr(tm,"kwargs");
    }
  }
  Delete(newop);
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_apply_multi()
 *
 * Multi-argument %apply directive.  This is pretty horrible so I sure hope
 * it works.
 * ----------------------------------------------------------------------------- */

static
int count_args(String *s) {
  /* Count up number of arguments */
  int na = 0;
  char *c = Char(s);
  while (*c) {
    if (*c == '+') na++;
    c++;
  }
  return na;
}

void 
Swig_typemap_apply_multi(ParmList *src, ParmList *dest) {
  String *ssig, *dsig;
  Parm   *p, *np, *lastp, *dp, *lastdp = 0;
  int     narg = 0;
  int     ts = tm_scope;
  SwigType *type = 0, *name;
  Hash     *tm, *sm;

  /* Create type signature of source */
  ssig = NewString("");
  dsig = NewString("");
  p = src;
  dp = dest;
  lastp = 0;
  while (p) {
    lastp = p;
    lastdp = dp;
    np = nextSibling(p);
    if (np) {
      Printf(ssig,"-%s+%s:", Getattr(p,"type"), Getattr(p,"name"));
      Printf(dsig,"-%s+%s:", Getattr(dp,"type"), Getattr(dp,"name"));
      narg++;
    }
    p = np;
    dp = nextSibling(dp);
  }

  /* make sure a typemap node exists for the last destination node */
  tm = Getattr(typemaps[tm_scope],Getattr(lastdp,"type"));
  if (!tm) {
    tm = NewHash();
    Setattr(typemaps[tm_scope],Copy(type),tm);
    Delete(tm);
  }
  name = Getattr(lastdp,"name");
  if (name) {
    Hash *tm1 = Getattr(tm,name);
    if (!tm1) {
      tm1 = NewHash();
      Setattr(tm,NewString(name),tm1);
      Delete(tm1);
    }
    tm = tm1;
  }

  /* This is a little nasty.  We need to go searching for all possible typemaps in the
     source and apply them to the target */

  type = Getattr(lastp,"type");
  name = Getattr(lastp,"name");

  while (ts >= 0) {

    /* See if there is a matching typemap in this scope */
    sm = Swig_typemap_get(type,name,ts);

    if (sm) {

      /* Got a typemap.  Need to only merge attributes for methods that match our signature */
      String *key;

      for (key = Firstkey(sm); key; key = Nextkey(sm)) {
	/* Check for a signature match with the source signature */
	if ((count_args(key) == narg) && (Strstr(key,ssig))) {

	  /* A typemap we have to copy */
	  String *nkey = Copy(key);
	  Replace(nkey,ssig,dsig,DOH_REPLACE_ANY);

	  /* Make sure the typemap doesn't already exist in the target map */
	  if (!Getattr(tm,nkey)) {
	    String *code;
	    ParmList *locals;
	    ParmList *kwargs;
	    Hash *sm1 = Getattr(sm,key);

	    code = Getattr(sm1,"code");
	    locals = Getattr(sm1,"locals");
	    kwargs = Getattr(sm1,"kwargs");
	    if (code) {
	      Replace(nkey,dsig,"", DOH_REPLACE_ANY);
	      Replace(nkey,"tmap:","", DOH_REPLACE_ANY);

	      Swig_typemap_register_multi(nkey,dest,code,locals,kwargs);
	    }
	  }
	  Delete(nkey);
	}
      }
    }
    ts--;
  }
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_clear_apply()
 *
 * %clear directive.   Clears all typemaps for a type (in the current scope only).    
 * ----------------------------------------------------------------------------- */

/* Multi-argument %clear directive */
void
Swig_typemap_clear_apply_multi(Parm *parms) {
  String *tsig;
  Parm   *p, *np, *lastp;
  int     narg = 0;
  Hash   *tm;
  String *name;

  /* Create a type signature of the parameters */
  tsig = NewString("");
  p = parms;
  lastp = 0;
  while (p) {
    lastp = p;
    np = nextSibling(p);
    if (np) {
      Printf(tsig,"-%s+%s:", Getattr(p,"type"), Getattr(p,"name"));
      narg++;
    }
    p = np;
  }
  tm = Getattr(typemaps[tm_scope],Getattr(lastp,"type"));
  if (!tm) {
    Delete(tsig);
    return;
  }
  name = Getattr(lastp,"name");
  if (name) {
    tm = Getattr(tm,name);
  }
  if (tm) {
    /* Clear typemaps that match our signature */
    String *key, *key2;
    for (key = Firstkey(tm); key; key = Nextkey(tm)) {
      if (Strncmp(key,"tmap:",5) == 0) {
	int na = count_args(key);
	if ((na == narg) && Strstr(key,tsig)) {
	  Hash *h = Getattr(tm,key);
	  for (key2 = Firstkey(h); key2; key2 = Nextkey(h)) {
	    Delattr(h,key2);
	  }
	}
      }
    }
  }
  Delete(tsig);
}

/* Internal function to strip array dimensions. */
static SwigType *strip_arrays(SwigType *type) {
  SwigType *t;
  int ndim;
  int i;
  t = Copy(type);
  ndim = SwigType_array_ndim(t);
  for (i = 0; i < ndim; i++) {
    SwigType_array_setdim(t,i,"ANY");
  }
  return t;
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_search()
 *
 * Search for a typemap match.    Tries to find the most specific typemap
 * that includes a 'code' attribute.
 * ----------------------------------------------------------------------------- */

Hash *
Swig_typemap_search(const String_or_char *op, SwigType *type, String_or_char *name) {
  Hash *result = 0, *tm, *tm1, *tma;
  Hash *backup = 0;
  SwigType *noarrays = 0;
  SwigType *primitive = 0;
  SwigType *ctype = 0;
  int ts;
  int isarray;
  String *cname = 0;
  SwigType *unstripped = 0;
  String   *tmop = tmop_name(op);

  if ((name) && Len(name)) cname = name;
  isarray = SwigType_isarray(type);
  ts = tm_scope;

  while (ts >= 0) {
    ctype = type;
    while (ctype) {
      /* Try to get an exact type-match */
      tm = Getattr(typemaps[ts],ctype);
      if (tm && cname) {
	tm1 = Getattr(tm,cname);
	if (tm1) {
	  result = Getattr(tm1,tmop);          /* See if there is a type-name match */
	  if (result && Getattr(result,"code")) goto ret_result;
	  if (result) backup = result;
	}
      }
      if (tm) {
	result = Getattr(tm,tmop);            /* See if there is simply a type match */
	if (result && Getattr(result,"code")) goto ret_result;
	if (result) backup = result;
      }
      
      if (isarray) {
	/* If working with arrays, strip away all of the dimensions and replace with "".
	   See if that generates a match */
	if (!noarrays) noarrays = strip_arrays(ctype);
	tma = Getattr(typemaps[ts],noarrays);
	if (tma && cname) {
	  tm1 = Getattr(tma,cname);
	  if (tm1) {
	    result = Getattr(tm1,tmop);       /* type-name match */
	    if (result && Getattr(result,"code")) goto ret_result;
	    if (result) backup = result;
	  }
	}
	if (tma) {
	  result = Getattr(tma,tmop);        /* type match */
	  if (result && Getattr(result,"code")) goto ret_result;
	  if (result) backup = result;
	}
      }
      
      /* No match so far.   If the type is unstripped, we'll strip its
         qualifiers and check.   Otherwise, we'll try to resolve a typedef */

      if (!unstripped) {
	unstripped = ctype;
	ctype = SwigType_strip_qualifiers(ctype);
	if (Cmp(ctype,unstripped)) continue;
	Delete(ctype);
	ctype = unstripped;
	unstripped = 0;
      }
      {
	if (unstripped) {
	  if (unstripped != type) Delete(unstripped);
	  unstripped = 0;
	}
	ctype = SwigType_typedef_resolve(ctype);
      }
    }
    
    /* Hmmm. Well, no match seems to be found at all. See if there is some kind of default mapping */
    if (!primitive)
      primitive = SwigType_default(type);
    tm = Getattr(typemaps[ts],primitive);
    if (tm && cname) {
      tm1 = Getattr(tm,cname);
      if (tm1) {
	result = Getattr(tm1,tmop);          /* See if there is a type-name match */
	if (result) goto ret_result;
      }
    }
    if (tm) {			/* See if there is simply a type match */
      result = Getattr(tm,tmop);
      if (result) goto ret_result;
    }
    if (ctype != type) Delete(ctype);
    ts--;         /* Hmmm. Nothing found in this scope.  Guess we'll go try another scope */
  }
  result = backup;

 ret_result:
  if (noarrays) Delete(noarrays);
  if (primitive) Delete(primitive);
  if ((unstripped) && (unstripped != type)) Delete(unstripped);
  if (type != ctype) Delete(ctype);
  return result;
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_search_multi()
 *
 * Search for a multi-valued typemap.
 * ----------------------------------------------------------------------------- */

Hash *
Swig_typemap_search_multi(const String_or_char *op, ParmList *parms, int *nmatch) {
  SwigType *type;
  String   *name;
  String   *newop;
  Hash     *tm, *tm1;

  if (!parms) {
    *nmatch = 0;
    return 0;
  }
  type = Getattr(parms,"type");
  name = Getattr(parms,"name");

  /* Try to find a match on the first type */
  tm = Swig_typemap_search(op, type, name);
  if (tm) {
    newop = NewStringf("%s-%s+%s:", op, type,name);
    tm1 = Swig_typemap_search_multi(newop, nextSibling(parms), nmatch);
    if (tm1) tm = tm1;
    if (Getattr(tm,"code")) {
      *(nmatch) = *nmatch + 1;
    } else {
      tm = 0;
    }
    Delete(newop);
  }
  return tm;
}


/* -----------------------------------------------------------------------------
 * typemap_replace_vars()
 *
 * Replaces typemap variables on a string.  index is the $n variable.
 * type and pname are the type and parameter name.
 * ----------------------------------------------------------------------------- */

static
void replace_local_types(ParmList *p, String *name, String *rep) {
  SwigType *t;
  while (p) {
    t = Getattr(p,"type");
    Replace(t,name,rep,DOH_REPLACE_ANY);
    p = nextSibling(p);
  }
}

static
void typemap_replace_vars(String *s, ParmList *locals, SwigType *type, String *pname, String *lname, int index) 
{
  char var[512];
  char *varname;

  if (!pname) pname = lname;
  {
    Parm *p;
    int  rep = 0;
    p = locals;
    while (p) {
      if (Strchr(Getattr(p,"type"),'$')) rep = 1;
      p = nextSibling(p);
    }
    if (!rep) locals = 0;
  }
  
  sprintf(var,"$%d_",index);
  varname = &var[strlen(var)];
    
  /* If the original datatype was an array. We're going to go through and substitute
     it's array dimensions */
    
  if (SwigType_isarray(type)) {
    int  ndim = SwigType_array_ndim(type);
    int i;
    for (i = 0; i < ndim; i++) {
      String *dim = SwigType_array_getdim(type,i);
      if (index == 1) {
	char t[32];
	sprintf(t,"$dim%d",i);
	Replace(s,t,dim,DOH_REPLACE_ANY);
	replace_local_types(locals,t,dim);	
      }
      sprintf(varname,"dim%d",i);
      Replace(s,var,dim,DOH_REPLACE_ANY);
      replace_local_types(locals,var,dim);	
      Delete(dim);
    }
  }

  /* Parameter name substitution */
  if (index == 1) {
    Replace(s,"$parmname",pname, DOH_REPLACE_ANY);
  }
  strcpy(varname,"name");
  Replace(s,var,pname,DOH_REPLACE_ANY);

  /* Type-related stuff */
  {
    SwigType *star_type, *amp_type, *base_type;
    SwigType *ltype, *star_ltype, *amp_ltype;
    String *mangle, *star_mangle, *amp_mangle, *base_mangle;
    String *descriptor, *star_descriptor, *amp_descriptor;
    String *ts;
    char   *sc;

    sc = Char(s);

    if (strstr(sc,"type")) {
      /* Given type : $type */
      ts = SwigType_str(type,0);
      if (index == 1) {
	Replace(s, "$type", ts, DOH_REPLACE_ANY);
	replace_local_types(locals,"$type",type);
      }
      strcpy(varname,"type");
      Replace(s,var,ts,DOH_REPLACE_ANY);
      replace_local_types(locals,var,type);
      Delete(ts);
      sc = Char(s);
    }
    if (strstr(sc,"ltype")) {
      /* Local type:  $ltype */
      ltype = SwigType_ltype(type);
      ts = SwigType_str(ltype,0);
      if (index == 1) {
	Replace(s, "$ltype", ts, DOH_REPLACE_ANY);
	replace_local_types(locals,"$ltype",ltype);
      }
      strcpy(varname,"ltype");
      Replace(s,var,ts,DOH_REPLACE_ANY);
      replace_local_types(locals,var,ltype);
      Delete(ts);
      Delete(ltype);
      sc = Char(s);
    }
    if (strstr(sc,"mangle") || strstr(sc,"descriptor")) {
      /* Mangled type */
      
      mangle = SwigType_manglestr(type);
      if (index == 1)
	Replace(s, "$mangle", mangle, DOH_REPLACE_ANY);
      strcpy(varname,"mangle");
      Replace(s,var,mangle,DOH_REPLACE_ANY);
    
      descriptor = NewStringf("SWIGTYPE%s", mangle);
      if (index == 1)
	if (Replace(s, "$descriptor", descriptor, DOH_REPLACE_ANY))
	  SwigType_remember(type);
      
      strcpy(varname,"descriptor");
      if (Replace(s,var,descriptor,DOH_REPLACE_ANY))
	SwigType_remember(type);
      
      Delete(descriptor);
      Delete(mangle);
    }
    
    /* One pointer level removed */
    /* This creates variables of the form
          $*n_type
          $*n_ltype
    */

    if (SwigType_ispointer(type)) {
      star_type = Copy(type);
      SwigType_del_pointer(star_type);
      ts = SwigType_str(star_type,0);
      if (index == 1) {
	Replace(s, "$*type", ts, DOH_REPLACE_ANY);
	replace_local_types(locals,"$*type",star_type);
      }
      sprintf(varname,"$*%d_type",index);
      Replace(s,varname,ts,DOH_REPLACE_ANY);
      replace_local_types(locals,varname,star_type);
      Delete(ts);
      
      star_ltype = SwigType_ltype(star_type);
      ts = SwigType_str(star_ltype,0);
      if (index == 1) {
	Replace(s, "$*ltype", ts, DOH_REPLACE_ANY);
	replace_local_types(locals,"$*ltype",star_ltype);
      }
      sprintf(varname,"$*%d_ltype",index);
      Replace(s,varname,ts,DOH_REPLACE_ANY);
      replace_local_types(locals,varname,star_ltype);
      Delete(ts);
      Delete(star_ltype);
      
      star_mangle = SwigType_manglestr(star_type);
      if (index == 1) 
	Replace(s, "$*mangle", star_mangle, DOH_REPLACE_ANY);
      
      sprintf(varname,"$*%d_mangle",index);
      Replace(s,varname,star_mangle,DOH_REPLACE_ANY);
      
      star_descriptor = NewStringf("SWIGTYPE%s", star_mangle);
      if (index == 1)
	if (Replace(s, "$*descriptor",
		    star_descriptor, DOH_REPLACE_ANY))
	  SwigType_remember(star_type);
      sprintf(varname,"$*%d_descriptor",index);
      if (Replace(s,varname,star_descriptor,DOH_REPLACE_ANY))
	SwigType_remember(star_type);
      
      Delete(star_descriptor);
      Delete(star_mangle);
      Delete(star_type);
    }
    else {
      /* TODO: Signal error if one of the $* substitutions is
	 requested */
    }
    /* One pointer level added */
    amp_type = Copy(type);
    SwigType_add_pointer(amp_type);
    ts = SwigType_str(amp_type,0);
    if (index == 1) {
      Replace(s, "$&type", ts, DOH_REPLACE_ANY);
      replace_local_types(locals,"$&type",amp_type);
    }
    sprintf(varname,"$&%d_type",index);
    Replace(s,varname,ts,DOH_REPLACE_ANY);
    replace_local_types(locals,varname,amp_type);
    Delete(ts);
    
    amp_ltype = SwigType_ltype(amp_type);
    ts = SwigType_str(amp_ltype,0);
    
    if (index == 1) {
      Replace(s, "$&ltype", ts, DOH_REPLACE_ANY);
      replace_local_types(locals, "$&ltype", amp_ltype);
    }
    sprintf(varname,"$&%d_ltype",index);
    Replace(s,varname,ts,DOH_REPLACE_ANY);
    replace_local_types(locals,varname,amp_ltype);
    Delete(ts);
    Delete(amp_ltype);
    
    amp_mangle = SwigType_manglestr(amp_type);
    if (index == 1) 
      Replace(s, "$&mangle", amp_mangle, DOH_REPLACE_ANY);
    sprintf(varname,"$&%d_mangle",index);
    Replace(s,varname,amp_mangle,DOH_REPLACE_ANY);
    
    amp_descriptor = NewStringf("SWIGTYPE%s", amp_mangle);
    if (index == 1) 
      if (Replace(s, "$&descriptor",
		  amp_descriptor, DOH_REPLACE_ANY))
	SwigType_remember(amp_type);
    sprintf(varname,"$&%d_descriptor",index);
    if (Replace(s,varname,amp_descriptor,DOH_REPLACE_ANY))
      SwigType_remember(amp_type);
    
    Delete(amp_descriptor);
    Delete(amp_mangle);
    Delete(amp_type);

    /* Base type */
    base_type = SwigType_base(type);
    if (index == 1) {
      Replace(s,"$basetype", base_type, DOH_REPLACE_ANY);
      replace_local_types(locals,"$basetype", base_type);
    }
    strcpy(varname,"basetype");
    Replace(s,var,base_type,DOH_REPLACE_ANY);
    replace_local_types(locals,var,base_type);
    
    base_mangle = SwigType_manglestr(base_type);
    if (index == 1)
      Replace(s,"$basemangle", base_mangle, DOH_REPLACE_ANY);
    strcpy(varname,"basemangle");
    Replace(s,var,base_mangle,DOH_REPLACE_ANY);
    Delete(base_mangle);
    Delete(base_type);
  }
  /* Replace the bare $n variable */
  sprintf(var,"$%d",index);
  Replace(s,var,lname,DOH_REPLACE_ANY);
}

/* ------------------------------------------------------------------------
 * static typemap_locals()
 *
 * Takes a string, a parameter list and a wrapper function argument and
 * creates the local variables.
 * ------------------------------------------------------------------------ */

static void typemap_locals(DOHString *s, ParmList *l, Wrapper *f, int argnum) {
  Parm *p;
  char *new_name;
  SwigType *pbase = 0;

  p = l;
  while (p) {
    SwigType *pt = Getattr(p,"type");
    String   *pn = Getattr(p,"name");
    if (pn) {
      if (Len(pn) > 0) {
	DOHString *str;

	str = NewString("");

	/* If the user gave us $type as the name of the local variable, we'll use
	   the passed datatype instead */

	if (argnum >= 0) {
	  Printf(str,"%s%d",pn,argnum);
	} else {
	  Printf(str,"%s",pn);
	}
	/* Substitute parameter names */
	/*	Replace(str,"$arg",pname, DOH_REPLACE_ANY);    /* This is deprecated (or should be) */
	new_name = Wrapper_new_localv(f,str, SwigType_str(pt,str), 0);
	/* Substitute  */
	Replace(s,pn,new_name,DOH_REPLACE_ID);
	Delete(pbase);
      }
    }
    p = nextSibling(p);
  }
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_lookup()
 *
 * Perform a typemap lookup (ala SWIG1.1)
 * ----------------------------------------------------------------------------- */

String *Swig_typemap_lookup(const String_or_char *op, SwigType *type, String_or_char *pname,
			  String_or_char *lname, String_or_char *source,
			  String_or_char *target, Wrapper *f) 
{
  Hash   *tm;
  String *s = 0;
  ParmList *locals;
  tm = Swig_typemap_search(op,type,pname);
  if (!tm) return 0;

  s = Getattr(tm,"code");
  if (!s) return 0;
  s = Copy(s);             /* Make a local copy of the typemap code */

  locals = Getattr(tm,"locals");
  if (locals) locals = CopyParmList(locals);

  /* This is wrong.  It replaces locals in place.   Need to fix this */
  typemap_replace_vars(s,locals,type,pname,lname,1);

  if (locals && f) {
    typemap_locals(s,locals,f,-1);
  }
   
  /* Now perform character replacements */
  Replace(s,"$source",source,DOH_REPLACE_ANY);
  Replace(s,"$target",target,DOH_REPLACE_ANY);
  {
    String *tmname = Getattr(tm,"typemap");
    if (tmname) Replace(s,"$typemap",tmname, DOH_REPLACE_ANY);
  }
  Replace(s,"$parmname",pname, DOH_REPLACE_ANY);
  /*  Replace(s,"$name",pname,DOH_REPLACE_ANY); */
  Delete(locals);
  return s;
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_attach_parms()
 *
 * Given a parmeter list, this function attaches all of the typemaps for a
 * given typemap type
 * ----------------------------------------------------------------------------- */

void
Swig_typemap_attach_parms(const String_or_char *op, ParmList *parms, Wrapper *f) {
  Parm *p, *firstp;
  Hash *tm;
  int   nmatch = 0;
  int   i;
  String *s;
  ParmList *locals;
  int   argnum = 0;
  char  temp[256];
  Parm  *kw;

  p = parms;
  while (p) {
    argnum++;
    nmatch = 0;
    tm = Swig_typemap_search_multi(op,p,&nmatch);
    if (!tm) {
      p = nextSibling(p);
      continue;
    }
    s = Getattr(tm,"code");
    if (!s) {
      p = nextSibling(p);
      continue;
    }

    s = Copy(s);
    locals = Getattr(tm,"locals");
    if (locals) locals = CopyParmList(locals);
    firstp = p;
    for (i = 0; i < nmatch; i++) {
      SwigType *type;
      String   *pname;
      String   *lname;

      type = Getattr(p,"type");
      pname = Getattr(p,"name");
      lname = Getattr(p,"lname");

      typemap_replace_vars(s,locals, type,pname,lname,i+1);
      p = nextSibling(p);
    }
    
    if (locals && f) {
      typemap_locals(s,locals,f,argnum);
    }

    /* Replace the argument number */
    sprintf(temp,"%d",argnum);
    Replace(s,"$argnum",temp, DOH_REPLACE_ANY);

    /* Attach attributes to object */
    Setattr(firstp,tmop_name(op),s);           /* Code object */

    /* Attach a link to the next parameter.  Needed for multimaps */
    sprintf(temp,"%s:next",Char(op));
    Setattr(firstp,tmop_name(temp),p);
    Delete(locals);

    /* Attach kwargs */
    kw = Getattr(tm,"kwargs");
    while (kw) {
      sprintf(temp,"%s:%s",Char(op),Char(Getattr(kw,"name")));
      Setattr(firstp,tmop_name(temp), Getattr(kw,"value"));
      kw = nextSibling(kw);
    }
  }
}

/* -----------------------------------------------------------------------------
 * Swig_typemap_debug()
 * ----------------------------------------------------------------------------- */

void Swig_typemap_debug() {
  int ts;
  Printf(stdout,"---[ typemaps ]--------------------------------------------------------------\n");
  
  ts = tm_scope;
  while (ts >= 0) {
    Printf(stdout,"::: scope %d\n\n",ts);
    Printf(stdout,"%s\n", typemaps[ts]);
    ts--;
  }
  Printf(stdout,"-----------------------------------------------------------------------------\n");
}
 

/* -----------------------------------------------------------------------------
 * %except directive support.
 *
 * These functions technically don't really have anything to do with typemaps
 * except that they have the same scoping rules.  Therefore, it's easy enough to 
 * just use the hash table structure of the typemap code.
 * ----------------------------------------------------------------------------- */

void Swig_except_register(String_or_char *code) {
  String *s = NewString(code);
  Setattr(typemaps[tm_scope],"*except*",s);
  Delete(s);
}

String *Swig_except_lookup() {
  String *s;
  int ts = tm_scope;
  while (ts >= 0) {
    s = Getattr(typemaps[tm_scope],"*except*");
    if (s) {
      s = Copy(s);
      return s;
    }
    ts--;
  }
  return 0;
}

void Swig_except_clear() {
  Delattr(typemaps[tm_scope],"*except*");
}
