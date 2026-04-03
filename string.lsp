;;
;;  fLisp string library
;;
;; Hugh Barney
;; leg20260331, CC 1.0
;;

(extension 'string)

;; (substring string[ start [end]])
(defun substring (string . args)
  (cond
    ((null args) string)
    ((and (integerp (car args)) (cdr args) (integerp (cadr args)))
     (elements string (char-offset string (car args)) (char-offset string (cadr args))) )
    ((integerp (car args))
     (elements string (char-offset string (car args))) )
    (t
     (error wrong-type-argument
	    (if (integerp (car args))
		"(substring string[ start [end]]) - end expected type-integer, got"
		"(substring string[ start [end]]) - start expected type-integer, got" )))))

;; trim all spaces from front of a string
(defun string-trim-front(s)
  (cond ((eq (substring s 0 1) " ") (string-trim-front (substring s 1)))
	(t s)))

;; trim all spaces from back of a string
(defun string-trim-back(s)
  (cond  ((eq (substring s -1)  " ") (string-trim-back (substring s 0 -1)))
	 (t s)))

;; trim spaces off front and back of a string
(defun string-trim(s)
  (string-trim-back (string-trim-front s)))

;;
;; string-ref , get character at position r
;;   zero based indexing
;;
(defun string-ref (s r)
   (substring s r (+ r 1)))

;;
;; string-startswith - return t if string starts with search
;;
(defun string-startswith (str search)
  (eq 0 (string-search search str)))

;;
;; shrink string right by dropping off the first char
;;
(defun string-shrink-right(s)
  (substring s 1))

;;
;; shrink string left by dropping off last char
;;
(defun string-shrink-left(s)
  (substring s 0 -1))

;;
;; return first char of string
;;
(defun string-first-char(s)
  (substring s 0 1))

;;
;; return last char of string
;;
(defun string-last-char(s)
  (substring s -1))

;;;
;;; Test if string is empty string
;;;
;;; Note: (i= 0 (string-length s)): might be faster
(defun string-empty-p (s)
  (string-equal "" s))

;;
;; Split string s at each substring f and return list of parts.
;; If f is the empty string, split all characters.
;;
(defun string-split (f s)
  (let loop ((parts nil) (s s) (i 0) (l (length f)))
       (cond
	 ((and (string-empty-p s) (string-empty-p f)) (nreverse parts))
	 ((null (setq i (string-search f s))) (nreverse (cons s parts)))
	 (t
	  (loop
	   (cons (substring s 0 (cond ((i= 0 l) 1) (t i))) parts)
	   (substring s (+ i (max l 1)))
	   0
	   l )))))

(provide 'string)
