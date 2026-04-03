;; -*-Lisp-*-
;;
;; Core fLisp extensions
;;

(bind t list (lambda args args))

(bind t defmacro
      (macro (name params . body)
	     (list 'bind t name (list (quote macro) params . body)) ))

;;; conditionals

(defmacro if (pred then . else)
  (cond (else  (list 'cond (list pred then)  (cons 't else)))
	(t     (list 'cond (list pred then))) ))

(defmacro if-not (pred else . then)
  (cond (then  (list 'cond (cons pred then) (list 't else)))
	(t     (list 'cond (list pred 'nil) (list 't else))) ))

(defmacro when (pred . body)
  (cond (body  (list 'cond (cons pred body)))) )

(defmacro unless (pred . body)
  (cond (body (list 'cond (list pred nil) (cons 't body)))) )

(defmacro defun (name params . body)
  (list 'bind t name (list (quote lambda) params . body)) )

;;; Accessors
;; Note: replace c*ddr with (elements n) where n = number of 'd's
(defun cadr (l) (car (cdr l)))
(defun cddr (l) (cdr (cdr l)))
(defun caddr (l) (car (cdr (cdr l))))
(defun caar (l)  (car (car l)))
(defun cdar (l)  (cdr (car l)))
(defun caaar (l) (car (car (car l))))
(defun cdaar (l) (cdr (car (car l))))

;; Note on setq
;; officially
;; (setq) => nil
;; (setq a) => error
;; but with bind:
;; (bind t x) => nil, x = nil
(defmacro setq args
  (when args
    (cons 'bind (cons t args)) ))

(defun curry (func arg1)
  (lambda (arg2) (func arg1 arg2)))

(defun typep (type object)  (same type (type-of object)))

(bind t
      integerp (curry typep type-integer)
      doublep  (curry typep type-double)
      stringp  (curry typep type-string)
      symbolp  (curry typep type-symbol)
      ;; consp is a primitive
      lambdap  (curry typep type-lambda)
      macrop   (curry typep type-macro)
      streamp  (curry typep type-stream)
      errorp   (curry typep type-error)
      vectorp  (curry typep type-vector)
      valuesp  (curry typep type-values) )

(defun mapcar (func xs)
  (cond (xs (cons (func (car xs)) (mapcar func (cdr xs))))))

;; (let bindings[ body])
;; (let label[ bindings[ body])
;; (let ()) => nil
;; (let (bindings)) => nil
(defmacro let (b-or-l . args)
  (cond
    ((null b-or-l) nil)
    ((consp b-or-l) ; ((bindings) body)
     (if args
	 (cons ; apply
	  (cons 'lambda (cons (mapcar car b-or-l) args)); vars
	  (mapcar cadr b-or-l) ))) ; values
    ((symbolp b-or-l) ; (label (bindings) body)
     ;; bindings: (car args)
     ;; body:     (cdr args)
     (list
      (list 'lambda ()
;;;	    (list 'define (car args)
	    (list 'bind nil b-or-l
		  (cons 'lambda (cons (mapcar car (car args)) (cdr args))))
	    (cons b-or-l (mapcar cadr (car args))) )))
    (t (error wrong-type-argument "(let bindings body) - bindings expected type-consp or type-symbol, got: " (type-of (car args)))) ))

;; (let* () body) => ((lambda () body))
;; (let* ((var val) ..) body) =>  ((lambda (var) (let* (..) body)) val)
(defmacro let* (bindings . body)
  (if (null bindings)  (list (cons 'lambda (cons (list) body)))
      (if (and (consp bindings)  (consp (car bindings)))
	  (cons (cons 'lambda (cons (list (caar bindings)) (list (cons let* (cons (cdr bindings) body)))))  (cdar bindings))
	  (error wrong-type-argument "(let* bindings[ body]) - bindings: does not start with a binding" bindings)) ))

(defun prog1 (arg . args) arg)

(defun string (o)
  ;; Convert argument to string.
  ;; Common Lisp
  (cond
    ((eq nil o) "")
    ((stringp o) o)
    ((symbolp o) (symbol-name o))
    ((consp o) (string-append (string (car o)) (string (cdr o))))
    (t (let ((f (open "" ">")))
	 (write o t f)
	 (prog1
	     (cadr (file-info f))
	   (close f) )))))

;; Concatenate all arguments to a string.
;; Elisp
(defun concat args
  (cond
    ((eq nil args) "")
    ((eq nil (cdr args)) (string (car args)))
    (t (string-append (string (car args)) (concat (cdr args)))) ))

(defun numberp (o) (cond  ((integerp o)) ((doublep o))))

; Schemish list filtering
(defun filter (p l)
  (when l
    (if (p (car l))  (cons (car l) (filter p (cdr l)))
	(filter p (cdr l)) )))

(defun remove (p l)
  (when l
    (if (p (car l))  (remove p (cdr l))
	(cons (car l) (remove p (cdr l))) )))

(defun fold-left (f i l)
  (if (null l)  i
      (fold-left f (f i (car l)) (cdr l)) ))

(defun flip (func)  (lambda (o1 o2) (func o2 o1)))
(defun reverse (l)  (fold-left (flip cons) nil l))

;;; https://www.scheme.com/tspl2d/objects.html#g2052
(defun append lists
  (let f ((ls nil) (lists lists))
       (if (null lists)  ls
	   (let g ((ls ls))
		(if (null ls)  (f (car lists) (cdr lists))
		    (if (consp ls) ls
			(throw invalid-value
			  (concat "(append lists) - list expected type-list, got " (type-of ls)) ))
		    (cons (car ls) (g (cdr ls))) )))))


(defun apply (f . args)
  (if  (null args)  (f)
       (let ((rev (reverse args)))
	 ;; if last element is list splice it
	 (if (consp (car rev))  (eval (cons f (append (reverse (cdr rev)) (car rev))))
	     (f . args)) )))

(defun print (o . fd)
  (if fd  (write o t (car fd))
      (write o t) ))

(defun princ (o . fd)
  (if fd  (write o nil (car fd))
      (write o nil) ))

(defun string-to-number (string)
  (let* ((f (open string "<"))
	 (n (read f))
	 (type (type-of n)) )
    (close f)
    (if (numberp n)  n
	(error invalid-value (concat "(string-to-number string) - string expected number, got: " type) n) )))

(defun string-equal (s1 s2)  (i=0 (string-compare s1 s2)))

(defun eq (o1 o2)
  (cond
    ((same o1 o2))
    ((same (type-of o1) (type-of o2))
     (cond
       ((stringp o1) (string-equal o1 o2))
       ((integerp o1) (i= o1 o2))
       ((doublep o1) (d= o1 o2))))))

(bind t not null)

(defun memq (o l)
  ;; If object o in list l return sublist of l starting with o, else nil.
  ;; Elisp
  (cond
    ((eq nil l) nil)
    ((eq o (car l)) l)
    (t (memq o (cdr l)))))

(defun map (f . lists)
  (let loop ((result  nil) (lists lists))
       (if (memq nil lists) (nreverse result)
	   ;; Note: setq is a no go here!
	   (setq result (cons (apply f (mapcar car lists)) result))
	   (loop result (mapcar cdr lists) ))))

;;; Wrap all math to Integer operations
(defun nfold (f i l);  (3)  (1 2 3)
  (cond
    ((null l) i)
    ((null (cdr l)) (f i (car l)))
    ( t (fold-left f (f (car l) (cadr l)) (cddr l)))))

(defun fold-leftp (predicate start list)
  (cond ((null list))
	((predicate start (car list)) (fold-leftp predicate (car list) (cdr list)))))

(cond ((errorp d=) ;; only integer operations available
       (defun + args (fold-left i+ 0 args))
       (defun - args (nfold     i- 0 args))
       (defun * args (fold-left i* 1 args))
       (defun / args (nfold     i/ 1 args))
       (defun =  (n . args) (fold-leftp i=  n args))
       (defun <  (n . args) (fold-leftp i<  n args))
       (defun <= (n . args) (fold-leftp i<= n args))
       (defun >  (n . args) (fold-leftp i>  n args))
       (defun >= (n . args) (fold-leftp i>= n args)) )
      (t ;; with floating point we need argument coercion for arithmetics
       (defun coerce (ifunc dfunc x y)
	 (cond  ((doublep x) (if (integerp y)  (dfunc x (double y))  (dfunc x y)))
		((doublep y) (if (integerp x)  (dfunc (double x) y)  (dfunc x y)))
		(t (ifunc x y)) ))
       
       (defun coercec (ifunc dfunc) ; coerce "curry"
	 (lambda (x y) (coerce ifunc dfunc x y)))
       
       (defun +  args (fold-left (coercec i+ d+)  0 args))
       (defun -  args (nfold     (coercec i- d-)  0 args))
       (defun *  args (fold-left (coercec i* d*)  1 args))
       (defun /  args (nfold     (coercec i/ d/)  1 args))
       (defun %  args (nfold     (coercec i% d%)  1 args))
       (defun =  (n . args) (fold-leftp (coercec i=  d=)  n args))
       (defun <  (n . args) (fold-leftp (coercec i<  d<)  n args))
       (defun <= (n . args) (fold-leftp (coercec i<= d<=)  n args))
       (defun >  (n . args) (fold-leftp (coercec i>  d>)  n args))
       (defun >= (n . args) (fold-leftp (coercec i>= d>=)  n args)) ))

(defun min (n . args)
  (if (numberp n)  (fold-left (lambda (a b) (cond ((> a b) b) (t a))) n args)
      (error invalid-value (concat "(min n[ arg ..]) - n expected number, got :" (type-of n)) n) ))

(defun max (n . args)
  (if (numberp n) (fold-left (lambda (a b) (cond ((< a b) b) (t a))) n args)
      (error invalid-value (concat "(max n[ arg ..]) - n expected number, got :" (type-of n)) n) ))

;;; Note: in CL and Scheme (or 'a) => a when a is unbound, and the value of a otherwise.
;;;   We don't know how to produce this (easily?) in fLisp
;;;   So, when a is unbound we throw an error
(defmacro or args
  (cond (args
	 (list 'cond (list (car args))
	       (list 't (cons 'or (cdr args)))))))

(defmacro and args
  (cond
    ((null args))
    ((null (cdr args)) (car args))
    (t (list 'cond (list (car args) (cons 'and (cdr args)))))))

(defun length (o)
  (cond
    ((null o) 0)
    ((stringp o) (i- (object-size o) 1))
    ((consp o)
     (fold-left (lambda (x y) (i+ x 1)) 0 o))
    ((vectorp o) (object-length o))
    ((valuesp o) (length (elements o)))
    (t (error wrong-type-argument "(length object) - object unknown length for this type" (type-of o)))))


;;; Concatenate each element of l with separator f
(defun join (f l)
  (let loop ((s "") (l l))
       (cond
	 ((null l) "")
	 ((null (cdr l)) (concat s (car l)))
	 (t  (loop (concat s (car l) f) (cdr l))) )))

;; load
(defun fload (f)
  (let loop ((o  nil) (r nil))
       (setq o (read f :eof))
       (if (eq o :eof)  r
	   (setq r (eval o))
	   (loop nil r) )))

(defun load args
  (let ((f (open (car args))))
    (if (eq type-error (type-of f)) f
	(prog1 (fload f)
	  (close f) ))))

;; Features
(setq features nil)

(defun provide args
  ;; args: (feature [subfeature ..])
  ;; Elisp, subfeatures not implemented
  (if (memq (car args) features)  (car args)
      (setq features (cons (car args) features)) ))

(defun require (feature . args)
  ;; args: (feature [filename [noerror]])
  ;; Elisp optional parameters not implemented
  (if (memq feature features)  feature
      ;; Emacs optionally uses provided filename here
      (let* ((path (concat script_dir "/" (symbol-name feature) ".lsp"))
	     (r (catch (load path))) )
	(and (null (car r)) (memq feature features) feature) )))

(provide 'core)
