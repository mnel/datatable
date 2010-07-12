
<!-- This is the project specific website template -->
<!-- It can be changed as liked or replaced by other content -->

<?php

$domain=ereg_replace('[^\.]*\.(.*)$','\1',$_SERVER['HTTP_HOST']);
$group_name=ereg_replace('([^\.]*)\..*$','\1',$_SERVER['HTTP_HOST']);
$themeroot='http://r-forge.r-project.org/themes/rforge/';

echo '<?xml version="1.0" encoding="UTF-8"?>';
?>
<!DOCTYPE html
	PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN"
	"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en   ">

  <head>
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
	<title><?php echo $group_name; ?></title>
	<link href="<?php echo $themeroot; ?>styles/estilo1.css" rel="stylesheet" type="text/css" />
  </head>

<body>

<! --- R-Forge Logo --- >
<table border="0" width="100%" cellspacing="0" cellpadding="0">
<tr><td>
<a href="/"><img src="<?php echo $themeroot; ?>/images/logo.png" border="0" alt="R-Forge Logo" /> </a> </td> </tr>
</table>

<!-- own website starts here, the following may be changed as you like -->

<h2>Welcome to data.table project!</h2>

<p>Fast subset, fast grouping and fast merge in a short and flexible syntax, for faster development.</p>

<p>Example: <code>data[a>3,sum(b*c),by=d]</code> where data has 4 columns (a,b,c,d).</p>

<p><ul><li>10+ times faster than <code>tapply()</code></li>
<li>100+ times faster than <code>==</code></li></ul></p>

<!-- end of project description -->

<p>Latest stable release: <a href="http://cran.r-project.org/package=data.table"><strong>1.4.1 on CRAN</strong></a></p>

<p>Vignettes:</p>
<ul><li><a href="http://cran.r-project.org/web/packages/data.table/vignettes/datatable-faq.pdf"><strong>FAQs</strong></a></li>
<li><a href="http://cran.r-project.org/web/packages/data.table/vignettes/datatable-intro.pdf"><strong>10 minute quick start introduction</strong></a></li>
<li><a href="http://cran.r-project.org/web/packages/data.table/vignettes/datatable-timings.pdf"><strong>Reproducible timings</strong></a></li></ul>

<p>Presentations:</p>
<ul><li><a href="http://files.meetup.com/1406240/Data%20munging%20with%20SQL%20and%20R.pdf"><strong>Data munging with SQL and R</strong></a>, Joshua Reich, Jan 2010</li>
<li><a href="http://www.londonr.org/LondonR-20090331/data.table.LondonR.pdf"><strong>Higher speed time series queries</strong></a>, Matthew Dowle, Jul 2009</li></ul>

<p><a href="http://crantastic.org/packages/data-table"><strong>User reviews</strong></a></p>

<p><a href="http://lists.r-forge.r-project.org/cgi-bin/mailman/listinfo/datatable-help"><strong>datatable-help</strong></a></p>

<p><a href="http://r-forge.r-project.org/projects/datatable/"><strong>Development summary on r-forge</strong></a></p>

<p><a href="http://www.youtube.com/watch?v=rvT8XThGA8o"><strong>YouTube Demo (8 mins)</strong></a></p>
<object width="960" height="745"><param name="movie" value="http://www.youtube.com/v/rvT8XThGA8o&amp;hl=en_GB&amp;fs=1?hd=1"></param><param name="allowFullScreen" value="true"></param><param name="allowscriptaccess" value="always"></param><embed src="http://www.youtube.com/v/rvT8XThGA8o&amp;hl=en_GB&amp;fs=1?hd=1" type="application/x-shockwave-flash" allowscriptaccess="always" allowfullscreen="true" width="960" height="745"></embed></object>

</body>
</html>

