;; flisp Language

;; Standard Lisp and Scheme functions

(require 'core)

(defun listp (o) (cond ((null o)) ((consp o))))

;; Note: use (elements l i) instead
(defun nthcdr (i l)
  (cond
    ((not (integerp i))
     (error wrong-type-argument
       (concat "(nthcdr i l) - i expected type-integer, got " (type-of i))
       i))
    ((< i 0) (error range-error "negative index" i))
    ((null l) nil)
    ((= 0 i) l)
   ((not (consp l))
    (error wrong-type-argument
      (concat "(nthcdr i l) - l expected type-cons, got " (type-of l))
      l ))
    (t (nthcdr (- i 1) (cdr l)))))

(defun nth (n list)
  (car (nthcdr n list)))

(defun fold-right (f o l)
  (cond
    ((null l) o)
    (t (f (car l) (fold-right f o (cdr l))))))

(defun unfold (func o p)
  (cond ((p o) (cons o nil))
	(t (cons o (unfold func (func o) p)))))

(defun iota (count . args)
  (let (
	(count (- count 1))
	(start (cond ((car args)) (t 0)))
	(step (cond ((cadr args)) (t 1))))
    (let (
	  (func (lambda (n) (setq count (- count 1)) (+ n step)))
	  (pred (lambda (n) (= 0 count))))
      (unfold func start pred))))


;;; property lists

(defun prop-get (l k . p)
  (cond ((not (consp l)) (error :invalid-value "(prop-get l k[ p]) - l is not a list" l)))
  (setq p (cond (p (car p)) (t eq)))
  (cond
     ((p (car l) k)
      (cond ((consp (cdr l)) (cadr l)) (t (cdr l))))
     (t (cond ((consp (cdr l)) ; (p' v ...)
	     (cond ((consp (cddr l)) (prop-get (cddr l) k))) )) )) )

;;
;; Commonly used Lisp functions which are not other wise used in the Femto libraries
;;
(defun atom (o) (null (consp o)))
(defun zerop (n) (= n 0))

(defun equal (o1 o2)
  (or (and (atom o1) (atom o2)
	   (eq o1 o2))
      (and (consp o1) (consp o2)
	   (equal (car o1) (car o2))
	   (equal (cdr o1) (cdr o2)))))

;; https://github.com/kanaka/mal/blob/master/process/guide.md#step-7-quoting
(defun quasiquote-splice (ast)
  (cond
    ((null ast) nil)
    ((and (consp (car ast)) (same (caar ast) 'splice-unquote)) (list 'append (cadar ast) (quasiquote-splice (cdr ast))))
    (t (list 'cons (quasiquote-unquote (car ast)) (quasiquote-splice (cdr ast)))) ))

(defun quasiquote-unquote (ast)
  (cond
    ((consp ast)
     (cond
       ((same (car ast) 'unquote) (cadr ast))
       (t (quasiquote-splice ast)) ))
    ((same (type-of ast) type-symbol) (list 'quote ast))
    (t ast) ))

(defmacro quasiquote (ast) (quasiquote-unquote ast))


(provide 'flisp)
