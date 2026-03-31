;;
;;  fLisp string library
;;
;; Hugh Barney
;; leg20260331, CC 1.0
;;

(extension 'string)

;; (substring string[ start [end]])
(defun substring (string . args)
  (if-not args string
	  (let ((start (car args)) (end (when (consp (cdr args) (cadr args)))))
	    (if-not (same (type-of start) type-integer)
		    (error wrong-type-argument
			   "(substring string[ start [end]]) - start expected type-integer, got"
			   (type-of start) )
		    (if (null end) (elements string (char-offset string start))
			(if-not (same (type-of end) type-integer)
				(error wrong-type-argument
				       "(substring string[ start [end]]) - end expected type-integer, got:"
				       (type-of end) )
				(elements (char-offset string start) (char-offset string end)) ))))))

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
